#include "platform/renderer.hpp"

#if defined(_WIN32)

#    include "EngineFactoryD3D11.h"
#    include "Win32NativeWindow.h"

namespace insland::platform
{

bool Renderer::init_backend(Window& window, const RendererDesc& desc)
{
    auto* factory = Diligent::GetEngineFactoryD3D11();
    if (!factory) return false;

    Diligent::EngineD3D11CreateInfo create_info;
    create_info.EnableValidation = desc.validation;

    factory->CreateDeviceAndContextsD3D11(create_info, &device_, &context_);
    if (!device_ || !context_) return false;

    Diligent::SwapChainDesc swap_desc;
    swap_desc.Width = window.width();
    swap_desc.Height = window.height();
    swap_desc.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    swap_desc.DepthBufferFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    swap_desc.IsPrimary = Diligent::True;

    Diligent::Win32NativeWindow native{window.native_handle()};
    factory->CreateSwapChainD3D11(device_, context_, swap_desc, Diligent::FullScreenModeDesc{}, native,
                                  &swap_chain_);

    if (!swap_chain_) return false;

    zero_to_one_ndc_ = true;
    return true;
}

} // namespace insland::platform

#endif // _WIN32
