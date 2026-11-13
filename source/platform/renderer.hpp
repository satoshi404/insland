#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - renderer front-end
// ---------------------------------------------------------------------------
// The renderer owns the Diligent device, immediate context and swap chain.  All
// higher level code (engine.cpp, pipeline.cpp) talks to the Diligent interfaces
// directly, which is what makes the same drawing code work with D3D11 on
// Windows and OpenGL/GLES on Linux and Android.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <vector>

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"

#include "core/types.hpp"
#include "platform/window.hpp"

namespace insland::platform
{

struct RendererDesc
{
    bool vsync = true;
    bool validation = false;
    bool enable_debug_markers = false;
};

class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(Window& window, const RendererDesc& desc = {});
    void shutdown();

    // Binds the swap chain back buffer / depth buffer and clears them.
    void begin_frame();

    // Presents the current frame.
    void end_frame();

    void resize(std::uint32_t width, std::uint32_t height);

    // Reads the current back buffer back to the CPU as tightly packed RGBA8.
    // Call after drawing and before end_frame()/Present().  Returns false when
    // the backend does not support CPU readback.
    bool capture_frame(std::vector<std::uint8_t>& rgba, std::uint32_t& width,
                       std::uint32_t& height);

    Diligent::IRenderDevice* device() const { return device_; }
    Diligent::IDeviceContext* context() const { return context_; }
    Diligent::ISwapChain* swap_chain() const { return swap_chain_; }

    Diligent::TEXTURE_FORMAT color_format() const { return color_format_; }
    Diligent::TEXTURE_FORMAT depth_format() const { return depth_format_; }

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    float aspect_ratio() const { return height_ ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f; }

    bool uses_zero_to_one_ndc() const { return zero_to_one_ndc_; }
    bool vsync() const { return vsync_; }

    void set_clear_color(const Color& color) { clear_color_ = color; }
    const Color& clear_color() const { return clear_color_; }

private:
    bool init_backend(Window& window, const RendererDesc& desc);

    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swap_chain_;

    Window* window_ = nullptr;

    Diligent::TEXTURE_FORMAT color_format_ = Diligent::TEX_FORMAT_UNKNOWN;
    Diligent::TEXTURE_FORMAT depth_format_ = Diligent::TEX_FORMAT_UNKNOWN;

    Color clear_color_{0.055f, 0.062f, 0.078f, 1.0f};

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool vsync_ = true;
    bool zero_to_one_ndc_ = false;
};

} // namespace insland::platform
