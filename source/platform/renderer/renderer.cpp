#include "platform/renderer.hpp"

#include "GraphicsTypes.h"
#include "Texture.h"

#include <cstring>
#include <vector>

namespace insland::platform
{

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    shutdown();
}

bool Renderer::init(Window& window, const RendererDesc& desc)
{
    shutdown();
    window_ = &window;
    vsync_ = desc.vsync;

    if (!init_backend(window, desc))
    {
        shutdown();
        return false;
    }

    if (!device_ || !context_ || !swap_chain_)
    {
        shutdown();
        return false;
    }

    const Diligent::SwapChainDesc& swap_desc = swap_chain_->GetDesc();
    color_format_ = swap_desc.ColorBufferFormat;
    depth_format_ = swap_desc.DepthBufferFormat;
    width_ = swap_desc.Width;
    height_ = swap_desc.Height;

    if (width_ == 0) width_ = window.width();
    if (height_ == 0) height_ = window.height();

    return true;
}

void Renderer::shutdown()
{
    if (context_) context_->Flush();
    swap_chain_.Release();
    context_.Release();
    device_.Release();
    window_ = nullptr;
    color_format_ = Diligent::TEX_FORMAT_UNKNOWN;
    depth_format_ = Diligent::TEX_FORMAT_UNKNOWN;
    width_ = 0;
    height_ = 0;
}

void Renderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (!swap_chain_ || width == 0 || height == 0) return;
    if (width == width_ && height == height_) return;

    swap_chain_->Resize(width, height);
    const Diligent::SwapChainDesc& swap_desc = swap_chain_->GetDesc();
    width_ = swap_desc.Width;
    height_ = swap_desc.Height;
    color_format_ = swap_desc.ColorBufferFormat;
    depth_format_ = swap_desc.DepthBufferFormat;
}

bool Renderer::capture_frame(std::vector<std::uint8_t>& rgba, std::uint32_t& width,
                             std::uint32_t& height)
{
    rgba.clear();
    width = 0;
    height = 0;

    if (!swap_chain_ || !context_ || !device_) return false;

    Diligent::ITextureView* rtv = swap_chain_->GetCurrentBackBufferRTV();
    if (!rtv) return false;
    Diligent::ITexture* source = rtv->GetTexture();
    if (!source) return false;

    const Diligent::TextureDesc& source_desc = source->GetDesc();
    if (source_desc.Width == 0 || source_desc.Height == 0) return false;

    Diligent::TextureDesc staging_desc;
    staging_desc.Name = "Insland frame readback";
    staging_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    staging_desc.Width = source_desc.Width;
    staging_desc.Height = source_desc.Height;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = source_desc.Format;
    staging_desc.SampleCount = 1;
    staging_desc.Usage = Diligent::USAGE_STAGING;
    staging_desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    staging_desc.BindFlags = Diligent::BIND_NONE;

    Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
    device_->CreateTexture(staging_desc, nullptr, &staging);
    if (!staging) return false;

    Diligent::CopyTextureAttribs copy(source, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                      staging, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->CopyTexture(copy);

    Diligent::MappedTextureSubresource mapped;
    context_->MapTextureSubresource(staging, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_NONE, nullptr, mapped);
    if (!mapped.pData)
    {
        context_->UnmapTextureSubresource(staging, 0, 0);
        return false;
    }

    width = source_desc.Width;
    height = source_desc.Height;
    rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    const std::uint8_t* row = static_cast<const std::uint8_t*>(mapped.pData);
    const bool bgra = source_desc.Format == Diligent::TEX_FORMAT_BGRA8_UNORM ||
                      source_desc.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB;

    for (std::uint32_t y = 0; y < height; ++y)
    {
        std::uint8_t* dst = rgba.data() + static_cast<std::size_t>(y) * width * 4u;
        const std::uint8_t* src = row + static_cast<std::size_t>(y) * mapped.Stride;
        if (bgra)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        }
        else
        {
            for (std::uint32_t x = 0; x < width * 4u; ++x) dst[x] = src[x];
        }
    }

    // OpenGL/GLES return texels bottom-up, while PNG (and every other
    // consumer of this buffer) expects a top-down image.  The NDC y-to-texture
    // scale is positive only on OpenGL-style backends, so use it to detect
    // the row order instead of hard-coding the backend type.
    if (device_->GetDeviceInfo().NDC.YtoVScale > 0.0f)
    {
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
        std::vector<std::uint8_t> scratch(row_bytes);
        for (std::uint32_t y = 0; y < height / 2; ++y)
        {
            std::uint8_t* top    = rgba.data() + static_cast<std::size_t>(y) * row_bytes;
            std::uint8_t* bottom = rgba.data() + static_cast<std::size_t>(height - 1 - y) * row_bytes;
            std::memcpy(scratch.data(), top, row_bytes);
            std::memcpy(top, bottom, row_bytes);
            std::memcpy(bottom, scratch.data(), row_bytes);
        }
    }

    context_->UnmapTextureSubresource(staging, 0, 0);
    return true;
}

void Renderer::begin_frame()
{
    if (!swap_chain_ || !context_) return;

    if (window_)
    {
        const std::uint32_t window_width = window_->width();
        const std::uint32_t window_height = window_->height();
        if (window_width > 0 && window_height > 0 &&
            (window_width != width_ || window_height != height_))
        {
            resize(window_width, window_height);
        }
    }

    Diligent::ITextureView* rtv = swap_chain_->GetCurrentBackBufferRTV();
    Diligent::ITextureView* dsv = swap_chain_->GetDepthBufferDSV();

    context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float clear[4] = {clear_color_.r, clear_color_.g, clear_color_.b, clear_color_.a};
    context_->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (dsv)
    {
        context_->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

void Renderer::end_frame()
{
    if (swap_chain_) swap_chain_->Present(vsync_ ? 1u : 0u);
}

} // namespace insland::platform
