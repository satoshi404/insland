#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - immediate mode UI (Dear ImGui + Diligent)
// ---------------------------------------------------------------------------
// A small backend that owns the ImGui context, the font atlas texture and the
// draw pipeline.  Game code only has to call three functions:
//
//     UI ui;
//     ui.init(*engine.renderer());
//
//     ui.begin_frame(engine.width(), engine.height(), dt);
//     ImGui::Begin("Inspector");
//     ImGui::Text("fps %.1f", 1.0f / dt);
//     ImGui::End();
//     ui.end_frame(*engine.renderer());
//
// Input is fed automatically from insland::input (mouse position, buttons,
// wheel and the common keys).
// ---------------------------------------------------------------------------

#include <cstdint>

#include "RefCntAutoPtr.hpp"
#include "Buffer.h"
#include "DeviceContext.h"
#include "PipelineState.h"
#include "RenderDevice.h"
#include "ShaderResourceBinding.h"
#include "Texture.h"

struct ImGuiContext;

namespace insland
{

namespace platform { class Renderer; }

class UI
{
public:
    UI() = default;
    ~UI();

    bool init(platform::Renderer& renderer);
    void shutdown();

    bool initialized() const { return pso_ != nullptr; }

    /// Starts a UI frame: feeds input, sets the display size and calls
    /// ImGui::NewFrame().
    void begin_frame(int width, int height, float delta_time);

    /// Renders the accumulated draw data and restores nothing (the caller owns
    /// the render state).
    void end_frame(platform::Renderer& renderer);

    bool wants_mouse() const;
    bool wants_keyboard() const;

    ImGuiContext* context() const { return context_; }

private:
    bool create_font_texture(platform::Renderer& renderer);
    bool ensure_buffers(platform::Renderer& renderer, std::uint32_t vertex_count,
                        std::uint32_t index_count);

    ImGuiContext* context_ = nullptr;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> projection_buffer_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertex_buffer_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> index_buffer_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> font_texture_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> font_view_;
    Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_;

    Diligent::IShaderResourceVariable* projection_variable_ = nullptr;
    Diligent::IShaderResourceVariable* texture_variable_ = nullptr;
    Diligent::IShaderResourceVariable* sampler_variable_ = nullptr;

    std::uint32_t vertex_capacity_ = 0;
    std::uint32_t index_capacity_ = 0;
};

} // namespace insland
