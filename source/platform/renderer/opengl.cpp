#include "platform/renderer.hpp"

#if !defined(_WIN32) || defined(INSLAND_FORCE_OPENGL)

#    include "EngineFactoryOpenGL.h"

#    include <cstdint>

namespace insland::platform
{

bool Renderer::init_backend(Window& window, const RendererDesc& desc)
{
    auto* factory = Diligent::GetEngineFactoryOpenGL();
    if (!factory) return false;

    Diligent::EngineGLCreateInfo create_info;
    create_info.EnableValidation = desc.validation;
    create_info.ZeroToOneNDZ = Diligent::False;

#    if defined(_WIN32)
    create_info.Window.hWnd = window.native_handle();
#    elif defined(__ANDROID__)
    create_info.Window.pAWindow = window.native_handle();
#    elif defined(__linux__)
    create_info.Window.WindowId = static_cast<Diligent::Uint32>(
        reinterpret_cast<std::uintptr_t>(window.native_handle()));
    create_info.Window.pDisplay = window.native_display();
#    endif

    Diligent::SwapChainDesc swap_desc;
    swap_desc.Width = window.width();
    swap_desc.Height = window.height();
    swap_desc.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    swap_desc.DepthBufferFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    swap_desc.IsPrimary = Diligent::True;

    factory->CreateDeviceAndSwapChainGL(create_info, &device_, &context_, swap_desc, &swap_chain_);
    if (!swap_chain_) return false;

    zero_to_one_ndc_ = false;
    return true;
}

} // namespace insland::platform

#endif // !_WIN32 || INSLAND_FORCE_OPENGL
