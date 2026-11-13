#include "platform/window.hpp"

#if defined(__linux__) && !defined(__ANDROID__)

#    include <X11/Xlib.h>
#    include <X11/keysym.h>

#    include <cstdint>
#    include <cstring>
#    include <string>

namespace insland::platform
{
namespace
{

input::Key map_keysym(KeySym keysym)
{
    using input::Key;

    if (keysym >= XK_a && keysym <= XK_z)
        return static_cast<Key>(static_cast<std::uint16_t>(Key::A) + static_cast<std::uint16_t>(keysym - XK_a));
    if (keysym >= XK_A && keysym <= XK_Z)
        return static_cast<Key>(static_cast<std::uint16_t>(Key::A) + static_cast<std::uint16_t>(keysym - XK_A));
    if (keysym >= XK_0 && keysym <= XK_9)
        return static_cast<Key>(static_cast<std::uint16_t>(Key::Num0) + static_cast<std::uint16_t>(keysym - XK_0));

    switch (keysym)
    {
        case XK_BackSpace: return Key::Backspace;
        case XK_Tab: return Key::Tab;
        case XK_Return:
        case XK_KP_Enter: return Key::Enter;
        case XK_Escape: return Key::Escape;
        case XK_space: return Key::Space;
        case XK_Insert: return Key::Insert;
        case XK_Delete: return Key::Delete;
        case XK_Home: return Key::Home;
        case XK_End: return Key::End;
        case XK_Page_Up: return Key::PageUp;
        case XK_Page_Down: return Key::PageDown;
        case XK_Left: return Key::Left;
        case XK_Right: return Key::Right;
        case XK_Up: return Key::Up;
        case XK_Down: return Key::Down;
        case XK_Shift_L: return Key::LeftShift;
        case XK_Shift_R: return Key::RightShift;
        case XK_Control_L: return Key::LeftControl;
        case XK_Control_R: return Key::RightControl;
        case XK_Alt_L: return Key::LeftAlt;
        case XK_Alt_R: return Key::RightAlt;
        case XK_Caps_Lock: return Key::CapsLock;
        case XK_Num_Lock: return Key::NumLock;
        case XK_Scroll_Lock: return Key::ScrollLock;
        case XK_Pause: return Key::Pause;
        case XK_Print: return Key::PrintScreen;
        case XK_Menu: return Key::Menu;

        case XK_KP_0: return Key::Keypad0;
        case XK_KP_1: return Key::Keypad1;
        case XK_KP_2: return Key::Keypad2;
        case XK_KP_3: return Key::Keypad3;
        case XK_KP_4: return Key::Keypad4;
        case XK_KP_5: return Key::Keypad5;
        case XK_KP_6: return Key::Keypad6;
        case XK_KP_7: return Key::Keypad7;
        case XK_KP_8: return Key::Keypad8;
        case XK_KP_9: return Key::Keypad9;
        case XK_KP_Add: return Key::KeypadAdd;
        case XK_KP_Subtract: return Key::KeypadSubtract;
        case XK_KP_Multiply: return Key::KeypadMultiply;
        case XK_KP_Divide: return Key::KeypadDivide;
        case XK_KP_Decimal: return Key::KeypadDecimal;

        default: break;
    }

    if (keysym >= XK_F1 && keysym <= XK_F12)
        return static_cast<Key>(static_cast<std::uint16_t>(Key::F1) + static_cast<std::uint16_t>(keysym - XK_F1));

    return Key::Unknown;
}

} // namespace

class X11Window final : public Window
{
public:
    ~X11Window() override
    {
        if (window_ && display_)
        {
            XDestroyWindow(display_, window_);
            window_ = 0;
        }
        if (display_)
        {
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }

    bool create(const WindowDesc& desc)
    {
        display_ = XOpenDisplay(nullptr);
        if (!display_) return false;

        screen_ = DefaultScreen(display_);
        width_ = desc.width ? desc.width : 1280;
        height_ = desc.height ? desc.height : 720;
        title_ = desc.title;

        window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen_), 0, 0, width_, height_, 0,
                                      BlackPixel(display_, screen_), WhitePixel(display_, screen_));
        if (!window_) return false;

        const long event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                                PointerMotionMask | StructureNotifyMask | FocusChangeMask;
        XSelectInput(display_, window_, event_mask);

        wm_delete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display_, window_, &wm_delete_, 1);

        XStoreName(display_, window_, title_.c_str());
        XMapWindow(display_, window_);
        XFlush(display_);

        running_ = true;
        return true;
    }

    bool attach(void* native, std::uint32_t width, std::uint32_t height)
    {
        if (!native) return false;
        display_ = XOpenDisplay(nullptr);
        if (!display_) return false;
        window_ = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(native));
        screen_ = DefaultScreen(display_);
        width_ = width;
        height_ = height;
        wm_delete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        XSelectInput(display_, window_, ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                                            ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | FocusChangeMask);
        running_ = true;
        return true;
    }

    bool poll_events() override
    {
        if (!display_ || !running_) return false;

        while (XPending(display_) > 0)
        {
            XEvent event{};
            XNextEvent(display_, &event);

            switch (event.type)
            {
                case ClientMessage:
                    if (static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_)
                        running_ = false;
                    break;

                case DestroyNotify:
                    running_ = false;
                    break;

                case ConfigureNotify:
                    if (event.xconfigure.width > 0 && event.xconfigure.height > 0)
                    {
                        width_ = static_cast<std::uint32_t>(event.xconfigure.width);
                        height_ = static_cast<std::uint32_t>(event.xconfigure.height);
                    }
                    break;

                case KeyPress:
                case KeyRelease:
                {
                    const KeySym keysym = XLookupKeysym(&event.xkey, 0);
                    const input::Key key = map_keysym(keysym);
                    if (key != input::Key::Unknown)
                        input::keyboard.set(key, event.type == KeyPress);
                    break;
                }

                case FocusOut:
                    for (std::size_t i = 0; i < input::kKeyCount; ++i)
                        input::keyboard.set(static_cast<input::Key>(i), false);
                    break;

                case MotionNotify:
                    input::mouse.move_to(static_cast<float>(event.xmotion.x), static_cast<float>(event.xmotion.y));
                    break;

                case ButtonPress:
                case ButtonRelease:
                {
                    const bool down = event.type == ButtonPress;
                    switch (event.xbutton.button)
                    {
                        case Button1: input::mouse.set(input::MouseButton::Left, down); break;
                        case Button2: input::mouse.set(input::MouseButton::Middle, down); break;
                        case Button3: input::mouse.set(input::MouseButton::Right, down); break;
                        case Button4:
                            if (down) input::mouse.wheel += 1.0f;
                            break;
                        case Button5:
                            if (down) input::mouse.wheel -= 1.0f;
                            break;
                        default:
                            if (event.xbutton.button == 8) input::mouse.set(input::MouseButton::X1, down);
                            else if (event.xbutton.button == 9) input::mouse.set(input::MouseButton::X2, down);
                            break;
                    }
                    break;
                }

                default: break;
            }
        }

        return running_;
    }

    void* native_handle() const override
    {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(window_));
    }

    void* native_display() const override { return display_; }

    std::uint32_t width() const override { return width_; }
    std::uint32_t height() const override { return height_; }
    bool should_close() const override { return !running_; }

    void set_should_close(bool value = true) override { running_ = !value; }

    void set_title(const std::string& title) override
    {
        title_ = title;
        if (display_ && window_) XStoreName(display_, window_, title_.c_str());
    }

    void set_size(std::uint32_t width, std::uint32_t height) override
    {
        if (!display_ || !window_ || width == 0 || height == 0) return;
        width_ = width;
        height_ = height;
        XResizeWindow(display_, window_, width_, height_);
        XFlush(display_);
    }

private:
    Display* display_ = nullptr;
    ::Window window_ = 0;
    int screen_ = 0;
    Atom wm_delete_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::string title_;
    bool running_ = false;
};

std::unique_ptr<Window> Window::create(const WindowDesc& desc)
{
    auto window = std::make_unique<X11Window>();
    if (!window->create(desc)) return nullptr;
    return window;
}

std::unique_ptr<Window> Window::from_native_handle(void* native, std::uint32_t width, std::uint32_t height)
{
    auto window = std::make_unique<X11Window>();
    if (!window->attach(native, width, height)) return nullptr;
    return window;
}

} // namespace insland::platform

#endif // __linux__ && !__ANDROID__
