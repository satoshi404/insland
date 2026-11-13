#include "pipeline.hpp"

#include "Shader.h"
#include "InputLayout.h"
#include "DepthStencilState.h"
#include "RasterizerState.h"
#include "BlendState.h"
#include "Texture.h"
#include "Sampler.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

namespace insland
{
namespace
{

// A single HLSL source is shared by every backend.  Diligent's OpenGL backend
// translates it to GLSL automatically (HLSL2GLSL), which is why the engine can
// use one shader for D3D11, desktop OpenGL and GLES.
const char* kMeshShaderSource = R"(
#define BONES_PER_BLOCK 128

cbuffer cbPerObject : register(b0)
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4x4 g_NormalMatrix;
    float4   g_BaseColor;
    float4   g_Params;          // x = use texture, y = skinned, z = metallic, w = roughness
    float4   g_CameraPosition;
    float4   g_LightDirection;
    float4   g_LightColor;
};

// The palette is split so that no uniform block exceeds 8 KB (see
// kMaxBones in pipeline.hpp).
cbuffer cbBones0 : register(b1) { float4x4 g_Bones0[BONES_PER_BLOCK]; };
cbuffer cbBones1 : register(b2) { float4x4 g_Bones1[BONES_PER_BLOCK]; };
cbuffer cbBones2 : register(b3) { float4x4 g_Bones2[BONES_PER_BLOCK]; };
cbuffer cbBones3 : register(b4) { float4x4 g_Bones3[BONES_PER_BLOCK]; };

float4x4 LoadBone(int index)
{
    // Clamp so that a file with more joints than the palette can hold reads a
    // valid (if incorrect) matrix instead of going out of bounds.
    if (index < 0) index = 0;
    if (index > BONES_PER_BLOCK * 4 - 1) index = BONES_PER_BLOCK * 4 - 1;

    if (index < BONES_PER_BLOCK)
        return g_Bones0[index];
    if (index < BONES_PER_BLOCK * 2)
        return g_Bones1[index - BONES_PER_BLOCK];
    if (index < BONES_PER_BLOCK * 3)
        return g_Bones2[index - BONES_PER_BLOCK * 2];
    return g_Bones3[index - BONES_PER_BLOCK * 3];
}

struct VSInput
{
    float3 Position : ATTRIB0;
    float3 Normal   : ATTRIB1;
    float2 UV       : ATTRIB2;
    float4 Tangent  : ATTRIB3;
    float4 Joints   : ATTRIB4;
    float4 Weights  : ATTRIB5;
    float4 Color    : ATTRIB6;
};

struct VSOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal       : TEXCOORD0;
    float2 UV           : TEXCOORD1;
    float3 WorldPos     : TEXCOORD2;
    float4 Color        : TEXCOORD3;
};

VSOutput VSMain(VSInput vin)
{
    float4 local_position = float4(vin.Position, 1.0);
    float3 local_normal   = vin.Normal;

    if (g_Params.y > 0.5)
    {
        // Use constructor-style casts instead of C-style casts: the HLSL to
        // GLSL converter passes them through verbatim, and GLSL only accepts
        // int(x) / float3x3(x), not (int)x / (float3x3)x.
        int i0 = int(vin.Joints.x);
        int i1 = int(vin.Joints.y);
        int i2 = int(vin.Joints.z);
        int i3 = int(vin.Joints.w);

        float4x4 skin =
            vin.Weights.x * LoadBone(i0) +
            vin.Weights.y * LoadBone(i1) +
            vin.Weights.z * LoadBone(i2) +
            vin.Weights.w * LoadBone(i3);

        // Extract the upper-left 3x3 block.  A C-style cast is valid HLSL but
        // illegal GLSL, while a float3x3(skin) matrix constructor is valid
        // GLSL but invalid HLSL.  The converted GLSL source defines GLSL.
#ifdef GLSL
        local_position = mul(skin, local_position);
        local_normal   = mul(float3x3(skin[0].xyz, skin[1].xyz, skin[2].xyz), local_normal);
#else
        local_position = mul(skin, local_position);
        local_normal   = mul((float3x3)skin, local_normal);
#endif
    }

    float4 world_position = mul(g_Model, local_position);

    VSOutput vout;
    vout.ClipPosition = mul(g_ViewProj, world_position);
#ifdef GLSL
    vout.Normal       = normalize(mul(float3x3(g_NormalMatrix[0].xyz, g_NormalMatrix[1].xyz, g_NormalMatrix[2].xyz), local_normal));
#else
    vout.Normal       = normalize(mul((float3x3)g_NormalMatrix, local_normal));
#endif
    vout.UV           = vin.UV;
    vout.WorldPos     = world_position.xyz;
    vout.Color        = vin.Color;
    return vout;
}

Texture2D    g_Texture : register(t0);
SamplerState g_Texture_sampler : register(s0);

// Diligent's HLSL2GLSL converter names interface variables after the shader
// parameter that owns them.  Vertex-shader outputs are owned by the entry
// point (VSMain), so on GL/GLES builds without explicit layout(location)
// qualifiers the pixel-shader parameter must carry the same name for the
// varyings to link.  Keep this parameter named "VSMain".
float4 PSMain(VSOutput VSMain) : SV_Target
{
    float4 texel = float4(1.0, 1.0, 1.0, 1.0);
    if (g_Params.x > 0.5)
        texel = g_Texture.Sample(g_Texture_sampler, VSMain.UV);

    float3 normal    = normalize(VSMain.Normal);
    float3 light_dir = normalize(-g_LightDirection.xyz);
    float  ndotl     = saturate(dot(normal, light_dir));

    float3 ambient = float3(0.16, 0.17, 0.20);
    float3 diffuse = g_LightColor.rgb * ndotl;
    // Sky light hack: vertices above the horizon add a subtle blue bounce.
    float  sky_up   = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);

    float3 color = g_BaseColor.rgb * texel.rgb * VSMain.Color.rgb * (ambient + diffuse);
    color += VSMain.Color.rgb * float3(0.05, 0.09, 0.16) * sky_up * 0.4;

    // Soft tonemap keeps untextured cubes pleasant without an HDR pipeline.
    color = color / (color + 1.0);

    return float4(color, g_BaseColor.a * texel.a * VSMain.Color.a);
}
)";

} // namespace

bool MeshPipeline::create(platform::Renderer& renderer)
{
    auto* device = renderer.device();
    if (!device) return false;

    Diligent::ShaderCreateInfo shader_ci;
    shader_ci.Source = kMeshShaderSource;
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    // The OpenGL backend converts HLSL to GLSL and requires textures and
    // samplers to be combined into a single GL sampler object.  The sampler is
    // matched to the texture by the "_sampler" name suffix (g_Texture +
    // g_Texture_sampler).  D3D11 ignores this flag.
    shader_ci.Desc.UseCombinedTextureSamplers = true;
    shader_ci.Desc.Name = "Insland mesh shader";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.EntryPoint = "VSMain";

    Diligent::RefCntAutoPtr<Diligent::IShader> vertex_shader;
    device->CreateShader(shader_ci, &vertex_shader);
    if (!vertex_shader) return false;

    shader_ci.Desc.Name = "Insland mesh pixel shader";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "PSMain";

    Diligent::RefCntAutoPtr<Diligent::IShader> pixel_shader;
    device->CreateShader(shader_ci, &pixel_shader);
    if (!pixel_shader) return false;

    Diligent::BufferDesc cb_desc;
    cb_desc.Name = "Insland uniform buffer";
    cb_desc.Size = sizeof(Uniforms);
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.Usage = Diligent::USAGE_DEFAULT;
    device->CreateBuffer(cb_desc, nullptr, &constant_buffer_);
    if (!constant_buffer_) return false;

    for (std::uint32_t block = 0; block < kBoneBlockCount; ++block)
    {
        Diligent::BufferDesc bone_desc;
        bone_desc.Name = "Insland bone block";
        bone_desc.Size = sizeof(BoneBlock);
        bone_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        bone_desc.Usage = Diligent::USAGE_DEFAULT;
        device->CreateBuffer(bone_desc, nullptr, &bone_buffers_[block]);
        if (!bone_buffers_[block]) return false;
    }

    const Diligent::LayoutElement layout[] = {
        {"ATTRIB", 0, 0, 3, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(Vertex, position)), sizeof(Vertex)},
        {"ATTRIB", 1, 0, 3, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(Vertex, normal)), sizeof(Vertex)},
        {"ATTRIB", 2, 0, 2, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(Vertex, uv)), sizeof(Vertex)},
        {"ATTRIB", 3, 0, 4, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(Vertex, tangent)), sizeof(Vertex)},
        {"ATTRIB", 4, 0, 4, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(Vertex, joints)), sizeof(Vertex)},
        {"ATTRIB", 5, 0, 4, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(Vertex, weights)), sizeof(Vertex)},
        {"ATTRIB", 6, 0, 4, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(Vertex, color)), sizeof(Vertex)},
    };

    const Diligent::ShaderResourceVariableDesc variables[] = {
        {Diligent::SHADER_TYPE_VERTEX | Diligent::SHADER_TYPE_PIXEL, "cbPerObject",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "cbBones0", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "cbBones1", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "cbBones2", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "cbBones3", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_PIXEL, "g_Texture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
    };

    Diligent::GraphicsPipelineStateCreateInfo pso_ci;
    pso_ci.PSODesc.Name = "Insland mesh PSO";
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
    pso_ci.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = Diligent::True;

    pso_ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = Diligent::True;
    pso_ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = Diligent::True;
    pso_ci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS;

    pso_ci.GraphicsPipeline.InputLayout.LayoutElements = layout;
    pso_ci.GraphicsPipeline.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layout));

    pso_ci.pVS = vertex_shader;
    pso_ci.pPS = pixel_shader;

    device->CreateGraphicsPipelineState(pso_ci, &pso_);
    if (!pso_)
    {
        std::fprintf(stderr, "[insland] CreateGraphicsPipelineState failed\n");
        return false;
    }

    pso_->CreateShaderResourceBinding(&srb_, true);
    if (!srb_)
    {
        std::fprintf(stderr, "[insland] CreateShaderResourceBinding failed\n");
        return false;
    }

    cb_variable_ = srb_->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "cbPerObject");
    texture_variable_ = srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
    sampler_variable_ = srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler");

    bool bones_ok = true;
    for (std::uint32_t block = 0; block < kBoneBlockCount; ++block)
    {
        const std::string name = "cbBones" + std::to_string(block);
        bone_variables_[block] = srb_->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, name.c_str());
        if (!bone_variables_[block])
        {
            std::fprintf(stderr, "[insland] missing shader variable %s\n", name.c_str());
            bones_ok = false;
        }
    }

    if (!cb_variable_) std::fprintf(stderr, "[insland] missing shader variable cbPerObject\n");
    if (!texture_variable_) std::fprintf(stderr, "[insland] missing shader variable g_Texture\n");
    // The sampler variable is optional: with combined texture samplers the
    // sampler lives on the texture view instead of being bound separately.
    // This is the case on every backend Insland currently targets, so its
    // absence is not an error.

    return cb_variable_ != nullptr && texture_variable_ != nullptr && bones_ok;
}

void MeshPipeline::shutdown()
{
    cb_variable_ = nullptr;
    for (std::uint32_t block = 0; block < kBoneBlockCount; ++block)
    {
        bone_variables_[block] = nullptr;
        bone_buffers_[block].Release();
    }
    texture_variable_ = nullptr;
    sampler_variable_ = nullptr;
    srb_.Release();
    pso_.Release();
    constant_buffer_.Release();
}

void MeshPipeline::draw(platform::Renderer& renderer, const Uniforms& uniforms,
                        const Mat4* bones, std::uint32_t bone_count,
                        Diligent::ITextureView* texture, Diligent::ISampler* sampler,
                        Diligent::IBuffer* vertex_buffer, Diligent::IBuffer* index_buffer,
                        std::uint32_t index_count)
{
    if (!pso_ || !srb_ || !vertex_buffer || !index_buffer || index_count == 0) return;

    auto* context = renderer.context();
    if (!context) return;

    context->SetPipelineState(pso_);

    context->UpdateBuffer(constant_buffer_, 0, sizeof(Uniforms), &uniforms,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    cb_variable_->Set(constant_buffer_);

    // Upload only the palette slices that are actually used.  Non-skinned draws
    // (cubes, static meshes) therefore pay nothing for skinning.
    for (std::uint32_t block = 0; block < kBoneBlockCount; ++block)
    {
        const std::uint32_t first = block * kBonesPerBlock;
        if (bones && first < bone_count)
        {
            BoneBlock data{};
            const std::uint32_t remaining = bone_count - first;
            const std::uint32_t count = remaining < kBonesPerBlock ? remaining : kBonesPerBlock;
            for (std::uint32_t i = 0; i < count; ++i) data.bones[i] = bones[first + i];
            context->UpdateBuffer(bone_buffers_[block], 0, sizeof(BoneBlock), &data,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        bone_variables_[block]->Set(bone_buffers_[block]);
    }

    if (texture)
    {
        // Combined sampler backends (OpenGL always, D3D11 when the shader was
        // created with UseCombinedTextureSamplers) read the sampler from the
        // texture view, so attach it there as well.
        if (sampler) texture->SetSampler(sampler);
        texture_variable_->Set(texture);
    }
    if (sampler && sampler_variable_) sampler_variable_->Set(sampler);

    context->CommitShaderResources(srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::IBuffer* vertex_buffers[] = {vertex_buffer};
    Diligent::Uint64 offsets[] = {0};
    context->SetVertexBuffers(0, 1, vertex_buffers, offsets,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    context->SetIndexBuffer(index_buffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawIndexedAttribs attribs;
    attribs.NumIndices = index_count;
    attribs.IndexType = Diligent::VT_UINT32;
    attribs.Flags = Diligent::DRAW_FLAG_NONE;
    context->DrawIndexed(attribs);
}

} // namespace insland
