// ---------------------------------------------------------------------------
// Insland Engine - immediate mode UI implementation (Dear ImGui + Diligent)
// ---------------------------------------------------------------------------

#include "ui/ui.hpp"

#include "core/logger.hpp"
#include "platform/keyboard.hpp"
#include "platform/mouse.hpp"
#include "platform/renderer.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>

namespace insland
{

namespace
{

constexpr char kUIShaderSource[] = R"(
cbuffer UIConstants
{
    float4x4 g_Projection;
};

struct VSInput
{
    float2 Pos   : ATTRIB0;
    float2 UV    : ATTRIB1;
    float4 Color : ATTRIB2;
};

struct VSOutput
{
    float4 Pos   : SV_Position;
    float2 UV    : TEXCOORD0;
    float4 Color : TEXCOORD1;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput result;
    result.Pos   = mul(g_Projection, float4(vin.Pos, 0.0, 1.0));
    result.UV    = vin.UV;
    result.Color = vin.Color;
    return result;
}

Texture2D    g_FontTexture : register(t0);
SamplerState g_FontTexture_sampler : register(s0);

// The parameter must be named after the vertex entry point so the GL/GLES
// HLSL2GLSL converter links the varyings by name (see pipeline.cpp).
float4 PSMain(VSOutput VSMain) : SV_Target
{
    return VSMain.Color * g_FontTexture.Sample(g_FontTexture_sampler, VSMain.UV);
}
)";

// ImGui stores vertices as { float2 pos; float2 uv; ImU32 col; }.
struct UIVertex
{
    float pos[2];
    float uv[2];
    std::uint32_t color;
};

static_assert(sizeof(UIVertex) == sizeof(ImDrawVert), "ImGui vertex layout changed");

struct KeyMapEntry
{
    input::Key key;
    ImGuiKey imgui;
};

const KeyMapEntry kKeyMap[] = {
    {input::Key::Tab, ImGuiKey_Tab},
    {input::Key::Backspace, ImGuiKey_Backspace},
    {input::Key::Enter, ImGuiKey_Enter},
    {input::Key::Escape, ImGuiKey_Escape},
    {input::Key::Space, ImGuiKey_Space},
    {input::Key::Delete, ImGuiKey_Delete},
    {input::Key::Home, ImGuiKey_Home},
    {input::Key::End, ImGuiKey_End},
    {input::Key::PageUp, ImGuiKey_PageUp},
    {input::Key::PageDown, ImGuiKey_PageDown},
    {input::Key::Left, ImGuiKey_LeftArrow},
    {input::Key::Right, ImGuiKey_RightArrow},
    {input::Key::Up, ImGuiKey_UpArrow},
    {input::Key::Down, ImGuiKey_DownArrow},
    {input::Key::LeftShift, ImGuiKey_LeftShift},
    {input::Key::RightShift, ImGuiKey_RightShift},
    {input::Key::LeftControl, ImGuiKey_LeftCtrl},
    {input::Key::RightControl, ImGuiKey_RightCtrl},
    {input::Key::LeftAlt, ImGuiKey_LeftAlt},
    {input::Key::RightAlt, ImGuiKey_RightAlt},
    {input::Key::A, ImGuiKey_A}, {input::Key::B, ImGuiKey_B}, {input::Key::C, ImGuiKey_C},
    {input::Key::D, ImGuiKey_D}, {input::Key::E, ImGuiKey_E}, {input::Key::F, ImGuiKey_F},
    {input::Key::G, ImGuiKey_G}, {input::Key::H, ImGuiKey_H}, {input::Key::I, ImGuiKey_I},
    {input::Key::J, ImGuiKey_J}, {input::Key::K, ImGuiKey_K}, {input::Key::L, ImGuiKey_L},
    {input::Key::M, ImGuiKey_M}, {input::Key::N, ImGuiKey_N}, {input::Key::O, ImGuiKey_O},
    {input::Key::P, ImGuiKey_P}, {input::Key::Q, ImGuiKey_Q}, {input::Key::R, ImGuiKey_R},
    {input::Key::S, ImGuiKey_S}, {input::Key::T, ImGuiKey_T}, {input::Key::U, ImGuiKey_U},
    {input::Key::V, ImGuiKey_V}, {input::Key::W, ImGuiKey_W}, {input::Key::X, ImGuiKey_X},
    {input::Key::Y, ImGuiKey_Y}, {input::Key::Z, ImGuiKey_Z},
    {input::Key::Num0, ImGuiKey_0}, {input::Key::Num1, ImGuiKey_1}, {input::Key::Num2, ImGuiKey_2},
    {input::Key::Num3, ImGuiKey_3}, {input::Key::Num4, ImGuiKey_4}, {input::Key::Num5, ImGuiKey_5},
    {input::Key::Num6, ImGuiKey_6}, {input::Key::Num7, ImGuiKey_7}, {input::Key::Num8, ImGuiKey_8},
    {input::Key::Num9, ImGuiKey_9},
};

} // namespace

UI::~UI()
{
    shutdown();
}

bool UI::init(platform::Renderer& renderer)
{
    if (context_) return true;
    if (!renderer.device()) return false;

    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    if (!context_) return false;
    ImGui::SetCurrentContext(context_);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;

    if (!create_font_texture(renderer)) return false;

    Diligent::ShaderCreateInfo shader_ci;
    shader_ci.Source = kUIShaderSource;
    shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shader_ci.Desc.UseCombinedTextureSamplers = true;
    shader_ci.Desc.Name = "Insland UI vertex shader";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shader_ci.EntryPoint = "VSMain";

    Diligent::RefCntAutoPtr<Diligent::IShader> vertex_shader;
    renderer.device()->CreateShader(shader_ci, &vertex_shader);
    if (!vertex_shader) return false;

    shader_ci.Desc.Name = "Insland UI pixel shader";
    shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shader_ci.EntryPoint = "PSMain";

    Diligent::RefCntAutoPtr<Diligent::IShader> pixel_shader;
    renderer.device()->CreateShader(shader_ci, &pixel_shader);
    if (!pixel_shader) return false;

    Diligent::BufferDesc projection_desc;
    projection_desc.Name = "Insland UI projection";
    projection_desc.Size = sizeof(Mat4);
    projection_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    projection_desc.Usage = Diligent::USAGE_DEFAULT;
    renderer.device()->CreateBuffer(projection_desc, nullptr, &projection_buffer_);
    if (!projection_buffer_) return false;

    const Diligent::LayoutElement layout[] = {
        {"ATTRIB", 0, 0, 2, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(UIVertex, pos)), sizeof(UIVertex)},
        {"ATTRIB", 1, 0, 2, Diligent::VT_FLOAT32, false, static_cast<Diligent::Uint32>(offsetof(UIVertex, uv)), sizeof(UIVertex)},
        {"ATTRIB", 2, 0, 4, Diligent::VT_UINT8, true, static_cast<Diligent::Uint32>(offsetof(UIVertex, color)), sizeof(UIVertex)},
    };

    const Diligent::ShaderResourceVariableDesc variables[] = {
        {Diligent::SHADER_TYPE_VERTEX, "UIConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_PIXEL, "g_FontTexture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_PIXEL, "g_FontTexture_sampler", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
    };

    Diligent::GraphicsPipelineStateCreateInfo pso_ci;
    pso_ci.PSODesc.Name = "Insland UI PSO";
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
    pso_ci.GraphicsPipeline.RasterizerDesc.ScissorEnable = Diligent::True;

    pso_ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = Diligent::False;
    pso_ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = Diligent::False;

    auto& blend = pso_ci.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = Diligent::True;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;

    pso_ci.GraphicsPipeline.InputLayout.LayoutElements = layout;
    pso_ci.GraphicsPipeline.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layout));

    pso_ci.pVS = vertex_shader;
    pso_ci.pPS = pixel_shader;

    renderer.device()->CreateGraphicsPipelineState(pso_ci, &pso_);
    if (!pso_)
    {
        std::fprintf(stderr, "[insland] UI pipeline creation failed\n");
        return false;
    }

    pso_->CreateShaderResourceBinding(&srb_, true);
    if (!srb_) return false;

    projection_variable_ = srb_->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "UIConstants");
    texture_variable_ = srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_FontTexture");
    sampler_variable_ = srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_FontTexture_sampler");

    if (!projection_variable_ || !texture_variable_)
    {
        std::fprintf(stderr, "[insland] UI shader variables missing\n");
        return false;
    }

    projection_variable_->Set(projection_buffer_);
    texture_variable_->Set(font_view_);
    if (sampler_variable_) sampler_variable_->Set(sampler_);

    Log::Info("UI", "ImGui backend ready");
    return true;
}

bool UI::create_font_texture(platform::Renderer& renderer)
{
    ImGuiIO& io = ImGui::GetIO();

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0) return false;

    Diligent::TextureDesc desc;
    desc.Name = "Insland UI font atlas";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<Diligent::Uint32>(width);
    desc.Height = static_cast<Diligent::Uint32>(height);
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.Usage = Diligent::USAGE_IMMUTABLE;

    Diligent::TextureSubResData subresource(pixels, static_cast<Diligent::Uint32>(width) * 4);
    Diligent::TextureData data(&subresource, 1);

    renderer.device()->CreateTexture(desc, &data, &font_texture_);
    if (!font_texture_) return false;

    font_view_ = font_texture_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    if (!font_view_) return false;

    Diligent::SamplerDesc sampler_desc;
    sampler_desc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler_desc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler_desc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler_desc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    renderer.device()->CreateSampler(sampler_desc, &sampler_);
    if (!sampler_) return false;

    // With combined texture samplers the sampler is attached to the view.  Do
    // it here so the very first bind of the font atlas is valid on D3D11 too
    // (not just on GL, where the texture/sampler are combined anyway).
    font_view_->SetSampler(sampler_);

    // Single texture: any non-null id is fine.
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    return true;
}

void UI::shutdown()
{
    if (!context_) return;

    ImGui::SetCurrentContext(context_);
    ImGui::DestroyContext(context_);
    context_ = nullptr;

    projection_variable_ = nullptr;
    texture_variable_ = nullptr;
    sampler_variable_ = nullptr;

    vertex_buffer_.Release();
    index_buffer_.Release();
    projection_buffer_.Release();
    srb_.Release();
    pso_.Release();
    sampler_.Release();
    font_view_.Release();
    font_texture_.Release();
}

void UI::begin_frame(int width, int height, float delta_time)
{
    if (!context_) return;
    ImGui::SetCurrentContext(context_);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = delta_time > 0.0f ? delta_time : 1.0f / 60.0f;

    io.MousePos = ImVec2(input::mouse.x, input::mouse.y);
    io.MouseDown[0] = input::mouse.is_down(input::MouseButton::Left);
    io.MouseDown[1] = input::mouse.is_down(input::MouseButton::Right);
    io.MouseDown[2] = input::mouse.is_down(input::MouseButton::Middle);
    io.MouseWheel = input::mouse.wheel;

    for (const KeyMapEntry& entry : kKeyMap)
        io.AddKeyEvent(entry.imgui, input::key_down(entry.key));

    ImGui::NewFrame();
}

bool UI::ensure_buffers(platform::Renderer& renderer, std::uint32_t vertex_count,
                        std::uint32_t index_count)
{
    if (vertex_count > vertex_capacity_)
    {
        Diligent::BufferDesc desc;
        desc.Name = "Insland UI vertices";
        desc.Size = static_cast<Diligent::Uint64>(vertex_count) * sizeof(UIVertex);
        desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
        desc.Usage = Diligent::USAGE_DEFAULT;
        vertex_buffer_.Release();
        renderer.device()->CreateBuffer(desc, nullptr, &vertex_buffer_);
        if (!vertex_buffer_) return false;
        vertex_capacity_ = vertex_count;
    }

    if (index_count > index_capacity_)
    {
        Diligent::BufferDesc desc;
        desc.Name = "Insland UI indices";
        desc.Size = static_cast<Diligent::Uint64>(index_count) * sizeof(ImDrawIdx);
        desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
        desc.Usage = Diligent::USAGE_DEFAULT;
        index_buffer_.Release();
        renderer.device()->CreateBuffer(desc, nullptr, &index_buffer_);
        if (!index_buffer_) return false;
        index_capacity_ = index_count;
    }
    return true;
}

void UI::end_frame(platform::Renderer& renderer)
{
    if (!context_) return;
    ImGui::SetCurrentContext(context_);

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->TotalVtxCount == 0) return;

    auto* context = renderer.context();
    if (!context) return;

    if (!ensure_buffers(renderer, static_cast<std::uint32_t>(draw_data->TotalVtxCount),
                        static_cast<std::uint32_t>(draw_data->TotalIdxCount)))
        return;

    // ImGui 1.93 stores vertices and indices per draw list, so pack them into
    // one contiguous upload and remember each list's base offset.
    std::vector<ImDrawVert> vertices(static_cast<std::size_t>(draw_data->TotalVtxCount));
    std::vector<ImDrawIdx> indices(static_cast<std::size_t>(draw_data->TotalIdxCount));
    std::vector<std::uint32_t> list_vertex_base(draw_data->CmdLists.Size, 0);
    std::vector<std::uint32_t> list_index_base(draw_data->CmdLists.Size, 0);

    std::uint32_t vertex_cursor = 0;
    std::uint32_t index_cursor = 0;
    for (int list_index = 0; list_index < draw_data->CmdLists.Size; ++list_index)
    {
        const ImDrawList* list = draw_data->CmdLists[list_index];
        list_vertex_base[list_index] = vertex_cursor;
        list_index_base[list_index] = index_cursor;

        if (list->VtxBuffer.Size > 0)
        {
            std::memcpy(vertices.data() + vertex_cursor, list->VtxBuffer.Data,
                        static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
            vertex_cursor += static_cast<std::uint32_t>(list->VtxBuffer.Size);
        }
        if (list->IdxBuffer.Size > 0)
        {
            std::memcpy(indices.data() + index_cursor, list->IdxBuffer.Data,
                        static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
            index_cursor += static_cast<std::uint32_t>(list->IdxBuffer.Size);
        }
    }

    context->UpdateBuffer(vertex_buffer_, 0,
                          static_cast<Diligent::Uint64>(vertex_cursor) * sizeof(ImDrawVert),
                          vertices.data(),
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->UpdateBuffer(index_buffer_, 0,
                          static_cast<Diligent::Uint64>(index_cursor) * sizeof(ImDrawIdx),
                          indices.data(),
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float width = draw_data->DisplaySize.x;
    const float height = draw_data->DisplaySize.y;
    const Mat4 projection = glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);

    context->UpdateBuffer(projection_buffer_, 0, sizeof(Mat4), &projection,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    context->SetPipelineState(pso_);
    projection_variable_->Set(projection_buffer_);
    texture_variable_->Set(font_view_);
    if (sampler_) font_view_->SetSampler(sampler_);
    if (sampler_variable_) sampler_variable_->Set(sampler_);
    context->CommitShaderResources(srb_, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const Diligent::Uint32 rt_width = static_cast<Diligent::Uint32>(width);
    const Diligent::Uint32 rt_height = static_cast<Diligent::Uint32>(height);

    for (int list_index = 0; list_index < draw_data->CmdLists.Size; ++list_index)
    {
        const ImDrawList* list = draw_data->CmdLists[list_index];

        Diligent::IBuffer* vertex_buffers[] = {vertex_buffer_};
        Diligent::Uint64 offsets[] = {0};

        for (int cmd_index = 0; cmd_index < list->CmdBuffer.Size; ++cmd_index)
        {
            const ImDrawCmd& cmd = list->CmdBuffer[cmd_index];
            if (cmd.ElemCount == 0 || cmd.UserCallback != nullptr) continue;

            offsets[0] = static_cast<Diligent::Uint64>(list_vertex_base[list_index] + cmd.VtxOffset) *
                         sizeof(ImDrawVert);
            context->SetVertexBuffers(0, 1, vertex_buffers, offsets,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                      Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
            context->SetIndexBuffer(
                index_buffer_,
                static_cast<Diligent::Uint64>(list_index_base[list_index] + cmd.IdxOffset) *
                    sizeof(ImDrawIdx),
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            const Diligent::Rect scissor{
                static_cast<Diligent::Int32>(cmd.ClipRect.x),
                static_cast<Diligent::Int32>(cmd.ClipRect.y),
                static_cast<Diligent::Int32>(cmd.ClipRect.z),
                static_cast<Diligent::Int32>(cmd.ClipRect.w)};
            context->SetScissorRects(1, &scissor, rt_width, rt_height);

            Diligent::DrawIndexedAttribs attribs;
            attribs.NumIndices = cmd.ElemCount;
            attribs.IndexType = sizeof(ImDrawIdx) == 2 ? Diligent::VT_UINT16 : Diligent::VT_UINT32;
            attribs.Flags = Diligent::DRAW_FLAG_NONE;
            context->DrawIndexed(attribs);
        }
    }
}

bool UI::wants_mouse() const
{
    return context_ && ImGui::GetIO().WantCaptureMouse;
}

bool UI::wants_keyboard() const
{
    return context_ && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace insland
