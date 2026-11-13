// ---------------------------------------------------------------------------
// Insland Engine - raymarched sky implementation
// ---------------------------------------------------------------------------

#include "graphics/sky.hpp"

#include "engine/camera.hpp"
#include "platform/renderer.hpp"

#include <cstdio>

namespace insland
{

namespace
{

constexpr char kSkyShaderSource[] = R"(
cbuffer SkyConstants
{
    float4x4 g_InvViewProj;
    float4   g_CameraPosition;   // xyz = camera, w = time
    float4   g_SunDirection;     // xyz = direction to the sun, w = intensity
    float4   g_ScreenSize;       // xy = size, zw = 1/size
};

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 NDC : TEXCOORD0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    float2 pos;
    if (vertex_id == 0u)
        pos = float2(-1.0, -1.0);
    else if (vertex_id == 1u)
        pos = float2(3.0, -1.0);
    else
        pos = float2(-1.0, 3.0);

    VSOutput result;
    result.Pos = float4(pos, 0.0, 1.0);
    // Pass the clip-space position as a varying.  Reading SV_Position in the
    // pixel shader maps to gl_FragCoord on OpenGL, whose Y origin is the
    // bottom of the framebuffer, which would flip the sky vertically.
    result.NDC = pos;
    return result;
}

float hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float value_noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + float3(0.0, 0.0, 0.0));
    float n100 = hash13(i + float3(1.0, 0.0, 0.0));
    float n010 = hash13(i + float3(0.0, 1.0, 0.0));
    float n110 = hash13(i + float3(1.0, 1.0, 0.0));
    float n001 = hash13(i + float3(0.0, 0.0, 1.0));
    float n101 = hash13(i + float3(1.0, 0.0, 1.0));
    float n011 = hash13(i + float3(0.0, 1.0, 1.0));
    float n111 = hash13(i + float3(1.0, 1.0, 1.0));

    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    float nxy0 = lerp(nx00, nx10, f.y);
    float nxy1 = lerp(nx01, nx11, f.y);
    return lerp(nxy0, nxy1, f.z);
}

float fbm(float3 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * value_noise(p);
        p *= 2.03;
        amplitude *= 0.5;
    }
    return value;
}

float cloud_density(float3 p, float time)
{
    float3 q = p * 0.006;
    q.y -= time * 0.004;
    q.x += time * 0.002;

    float n = fbm(q * 1.7);
    // Keep most of the dome clear: only the denser noise cells become cloud.
    float coverage = smoothstep(0.50, 0.74, n);

    float height_fade = saturate((p.y - 160.0) / 260.0) * saturate((860.0 - p.y) / 380.0);
    return coverage * height_fade;
}

// The parameter must be named after the vertex entry point so the GL/GLES
// HLSL2GLSL converter links the varyings by name (see pipeline.cpp).
float4 PSMain(VSOutput VSMain) : SV_Target
{
    // Reconstruct the view ray from the inverse view-projection matrix.
    float2 ndc = VSMain.NDC;
    float4 far_point = mul(g_InvViewProj, float4(ndc, 1.0, 1.0));
    float3 origin = g_CameraPosition.xyz;
    float3 dir = normalize(far_point.xyz / far_point.w - origin);

    float3 sun = normalize(g_SunDirection.xyz);
    float sun_dot = saturate(dot(dir, sun));

    // Atmosphere gradient.
    float horizon = pow(saturate(1.0 - abs(dir.y)), 6.0);
    float3 zenith = float3(0.055, 0.19, 0.46);
    float3 horizon_color = float3(0.56, 0.70, 0.88);
    float3 color = lerp(zenith, horizon_color, horizon);

    color += float3(1.0, 0.86, 0.62) * pow(sun_dot, 900.0) * 2.4 * g_SunDirection.w;
    color += float3(1.0, 0.72, 0.42) * pow(sun_dot, 10.0) * 0.12 * g_SunDirection.w;

    // Below the horizon fade to a dark ground tone.
    if (dir.y < 0.0)
    {
        float3 ground = float3(0.045, 0.055, 0.07);
        color = lerp(color, ground, saturate(-dir.y * 5.0));
    }

    // Volumetric clouds: march the ray through the layer [140, 900].
    if (dir.y > 0.01)
    {
        float t_enter = (140.0 - origin.y) / dir.y;
        float t_exit = (900.0 - origin.y) / dir.y;
        float t0 = max(t_enter, 0.0);
        // Capping the march keeps the deck from integrating to a flat opaque
        // sheet when viewed edge-on, which preserves distinct cloud shapes.
        float t1 = min(t_exit, 2600.0);

        if (t1 > t0)
        {
            const float step_length = (t1 - t0) / 40.0;
            // Fade the deck out towards the horizon so it dissolves into haze
            // instead of forming a solid white band.
            const float horizon_fade = smoothstep(0.0, 0.18, dir.y);
            float transmittance = 1.0;
            float3 scattered = float3(0.0, 0.0, 0.0);
            float t = t0;

            for (int i = 0; i < 40; ++i)
            {
                float3 p = origin + dir * t;
                float density = cloud_density(p, g_CameraPosition.w) * horizon_fade;
                if (density > 0.001)
                {
                    float light = 0.42 + 0.58 * saturate(p.y / 900.0);
                    float3 shade = lerp(float3(0.40, 0.45, 0.55), float3(0.90, 0.92, 0.95), light);
                    shade += float3(1.0, 0.8, 0.5) * pow(sun_dot, 8.0) * 0.22;

                    // Softer extinction keeps the cloud deck from turning the
                    // whole dome into a flat white sheet.
                    float alpha = saturate(density * step_length * 0.005);
                    scattered += shade * alpha * transmittance;
                    transmittance *= (1.0 - alpha);
                }
                t += step_length;
            }

            color = color * transmittance + scattered;
        }
    }

    // Same soft tonemap as the mesh pipeline.
    color = color / (color + 1.0);
    return float4(color, 1.0);
}
)";

struct SkyUniforms
{
    alignas(16) Mat4 inv_view_proj{1.0f};
    alignas(16) Vec4 camera_position{0.0f};
    alignas(16) Vec4 sun_direction{0.0f};
    alignas(16) Vec4 screen_size{0.0f};
};

} // namespace

bool Sky::create(platform::Renderer& renderer)
{
    auto* device = renderer.device();
    if (!device) return false;

    Diligent::ShaderCreateInfo shader_ci;
    shader_ci.Source = kSkyShaderSource;
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shader_ci.Desc.UseCombinedTextureSamplers = true;
    shader_ci.Desc.Name = "Insland sky vertex shader";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.EntryPoint = "VSMain";

    Diligent::RefCntAutoPtr<Diligent::IShader> vertex_shader;
    device->CreateShader(shader_ci, &vertex_shader);
    if (!vertex_shader) return false;

    shader_ci.Desc.Name = "Insland sky pixel shader";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "PSMain";

    Diligent::RefCntAutoPtr<Diligent::IShader> pixel_shader;
    device->CreateShader(shader_ci, &pixel_shader);
    if (!pixel_shader) return false;

    Diligent::BufferDesc cb_desc;
    cb_desc.Name = "Insland sky constants";
    cb_desc.Size = sizeof(SkyUniforms);
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.Usage = Diligent::USAGE_DEFAULT;
    device->CreateBuffer(cb_desc, nullptr, &constant_buffer_);
    if (!constant_buffer_) return false;

    const Diligent::ShaderResourceVariableDesc variables[] = {
        {Diligent::SHADER_TYPE_PIXEL, "SkyConstants",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
    };

    Diligent::GraphicsPipelineStateCreateInfo pso_ci;
    pso_ci.PSODesc.Name = "Insland sky PSO";
    pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pso_ci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    pso_ci.PSODesc.ResourceLayout.DefaultVariableMergeStages =
        Diligent::SHADER_TYPE_VERTEX | Diligent::SHADER_TYPE_PIXEL;
    pso_ci.PSODesc.ResourceLayout.NumVariables = static_cast<Diligent::Uint32>(std::size(variables));
    pso_ci.PSODesc.ResourceLayout.Variables = variables;

    pso_ci.GraphicsPipeline.NumRenderTargets = 1;
    pso_ci.GraphicsPipeline.RTVFormats[0] = renderer.color_format();
    pso_ci.GraphicsPipeline.DSVFormat = renderer.depth_format();
    pso_ci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    pso_ci.GraphicsPipeline.RasterizerDesc.FillMode = Diligent::FILL_MODE_SOLID;
    pso_ci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;

    pso_ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = Diligent::False;
    pso_ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = Diligent::False;

    pso_ci.pVS = vertex_shader;
    pso_ci.pPS = pixel_shader;

    device->CreateGraphicsPipelineState(pso_ci, &pso_);
    if (!pso_)
    {
        std::fprintf(stderr, "[insland] sky pipeline creation failed\n");
        return false;
    }

    pso_->CreateShaderResourceBinding(&srb_, true);
    if (!srb_) return false;

    cb_variable_ = srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "SkyConstants");
    if (!cb_variable_) return false;
    cb_variable_->Set(constant_buffer_);

    return true;
}

void Sky::shutdown()
{
    cb_variable_ = nullptr;
    srb_.Release();
    pso_.Release();
    constant_buffer_.Release();
}

void Sky::draw(platform::Renderer& renderer, const Camera& camera, double time)
{
    if (!pso_ || !srb_) return;

    auto* context = renderer.context();
    if (!context) return;

    const float width = static_cast<float>(renderer.width());
    const float height = static_cast<float>(renderer.height());
    if (width <= 0.0f || height <= 0.0f) return;

    SkyUniforms uniforms;
    const Mat4 view_projection = camera.view_projection(width / height);
    uniforms.inv_view_proj = glm::inverse(view_projection);
    uniforms.camera_position = Vec4(camera.position, static_cast<float>(time));
    uniforms.sun_direction = Vec4(sun_direction_, sun_intensity_);
    uniforms.screen_size = Vec4(width, height, 1.0f / width, 1.0f / height);

    context->SetPipelineState(pso_);
    context->UpdateBuffer(constant_buffer_, 0, sizeof(SkyUniforms), &uniforms,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    cb_variable_->Set(constant_buffer_);
    context->CommitShaderResources(srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawAttribs attribs;
    attribs.NumVertices = 3;
    attribs.Flags = Diligent::DRAW_FLAG_NONE;
    context->Draw(attribs);
}

} // namespace insland
