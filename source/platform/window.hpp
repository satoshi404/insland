#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - cross-platform window abstraction
// ---------------------------------------------------------------------------
// windows.cpp, linux.cpp and android.cpp provide the implementation selected by
// CMake.  The renderer only needs an opaque native handle; everything else is
// handled by the platform layer.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>

#include "platform/keyboard.hpp"
#include "platform/mouse.hpp"
#include "platform/touch.hpp"

namespace insland::platform
{

struct WindowDesc
{
    std::string title = "Insland Engine";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = true;
};

class Window
{
public:
    virtual ~Window() = default;

    // Creates a desktop window (Win32 / X11).  On Android this returns nullptr:
    // the native window is supplied by the Android activity instead.
    static std::unique_ptr<Window> create(const WindowDesc& desc);

    // Wraps an already existing native window.  On Android `native` is an
    // ANativeWindow*; on the desktop it is the platform window handle.
    static std::unique_ptr<Window> from_native_handle(void* native, std::uint32_t width,
                                                      std::uint32_t height);

    // Processes the platform event queue.  Returns false once the window has
    // been closed.
    virtual bool poll_events() = 0;

    virtual void* native_handle() const = 0;
    virtual void* native_display() const { return nullptr; }

    virtual std::uint32_t width() const = 0;
    virtual std::uint32_t height() const = 0;
    virtual bool should_close() const = 0;
    virtual void set_should_close(bool value = true) = 0;

    virtual void set_title(const std::string& title) = 0;

    // Resizes the native client area.  The renderer observes the new size on
    // the next frame and resizes its swap chain automatically.
    virtual void set_size(std::uint32_t width, std::uint32_t height) = 0;

    virtual bool is_key_down(input::Key key) const { return input::keyboard.is_down(key); }
    virtual bool is_mouse_down(input::MouseButton button) const { return input::mouse.is_down(button); }
};

// Called by the engine at the end of every frame.
inline void end_input_frame()
{
    input::keyboard.end_frame();
    input::mouse.end_frame();
    input::touch.end_frame();
}

} // namespace insland::platform
