#include "platform/window.hpp"

#if defined(_WIN32)

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <windowsx.h>

#    include <string>

namespace insland::platform
{
namespace
{

std::wstring utf8_to_wide(const std::string& text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

input::Key map_virtual_key(WPARAM key)
{
    using input::Key;

    if (key >= 'A' && key <= 'Z') return static_cast<Key>(key);
    if (key >= '0' && key <= '9') return static_cast<Key>(key);

    switch (key)
    {
        case VK_BACK: return Key::Backspace;
        case VK_TAB: return Key::Tab;
        case VK_RETURN: return Key::Enter;
        case VK_ESCAPE: return Key::Escape;
        case VK_SPACE: return Key::Space;
        case VK_INSERT: return Key::Insert;
        case VK_DELETE: return Key::Delete;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_LSHIFT: return Key::LeftShift;
        case VK_RSHIFT: return Key::RightShift;
        case VK_LCONTROL: return Key::LeftControl;
        case VK_RCONTROL: return Key::RightControl;
        case VK_LMENU: return Key::LeftAlt;
        case VK_RMENU: return Key::RightAlt;
        case VK_CAPITAL: return Key::CapsLock;
        case VK_NUMLOCK: return Key::NumLock;
        case VK_SCROLL: return Key::ScrollLock;
        case VK_PAUSE: return Key::Pause;
        case VK_SNAPSHOT: return Key::PrintScreen;
        case VK_APPS: return Key::Menu;
        case VK_NUMPAD0: return Key::Keypad0;
        case VK_NUMPAD1: return Key::Keypad1;
        case VK_NUMPAD2: return Key::Keypad2;
        case VK_NUMPAD3: return Key::Keypad3;
        case VK_NUMPAD4: return Key::Keypad4;
        case VK_NUMPAD5: return Key::Keypad5;
        case VK_NUMPAD6: return Key::Keypad6;
        case VK_NUMPAD7: return Key::Keypad7;
        case VK_NUMPAD8: return Key::Keypad8;
        case VK_NUMPAD9: return Key::Keypad9;
        case VK_ADD: return Key::KeypadAdd;
        case VK_SUBTRACT: return Key::KeypadSubtract;
        case VK_MULTIPLY: return Key::KeypadMultiply;
        case VK_DIVIDE: return Key::KeypadDivide;
        case VK_DECIMAL: return Key::KeypadDecimal;
        default: break;
    }

    if (key >= VK_F1 && key <= VK_F12)
        return static_cast<Key>(static_cast<std::uint16_t>(Key::F1) + (key - VK_F1));

    // Generic modifiers: pick the left variant.  WM_KEYDOWN for the specific
    // VK_LSHIFT/VK_RSHIFT messages normally arrives as well and refines it.
    if (key == VK_SHIFT) return Key::LeftShift;
    if (key == VK_CONTROL) return Key::LeftControl;
    if (key == VK_MENU) return Key::LeftAlt;

    return Key::Unknown;
}

} // namespace

class Win32Window final : public Window
{
public:
    Win32Window() = default;

    ~Win32Window() override
    {
        if (hwnd_)
        {
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            if (owns_hwnd_) DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    bool create(const WindowDesc& desc)
    {
        width_ = desc.width ? desc.width : 1280;
        height_ = desc.height ? desc.height : 720;
        title_ = desc.title;

        const HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(WNDCLASSEXW);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &Win32Window::window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = L"InslandWindowClass";

        if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        DWORD style = WS_OVERLAPPEDWINDOW;
        if (!desc.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        if (desc.fullscreen) style = WS_POPUP;

        RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
        AdjustWindowRectEx(&rect, style, FALSE, 0);

        hwnd_ = CreateWindowExW(0, window_class.lpszClassName, utf8_to_wide(title_).c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                rect.bottom - rect.top, nullptr, nullptr, instance, this);
        if (!hwnd_) return false;

        owns_hwnd_ = true;
        running_ = true;

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
    }

    bool attach(void* native, std::uint32_t width, std::uint32_t height)
    {
        hwnd_ = static_cast<HWND>(native);
        if (!hwnd_) return false;
        width_ = width;
        height_ = height;
        running_ = true;
        owns_hwnd_ = false;
        return true;
    }

    bool poll_events() override
    {
        if (!running_) return false;

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                running_ = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        return running_;
    }

    void* native_handle() const override { return hwnd_; }
    std::uint32_t width() const override { return width_; }
    std::uint32_t height() const override { return height_; }
    bool should_close() const override { return !running_; }

    void set_should_close(bool value = true) override
    {
        running_ = !value;
        if (value && hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }

    void set_title(const std::string& title) override
    {
        title_ = title;
        if (hwnd_) SetWindowTextW(hwnd_, utf8_to_wide(title_).c_str());
    }

    void set_size(std::uint32_t width, std::uint32_t height) override
    {
        if (!hwnd_ || width == 0 || height == 0) return;
        width_ = width;
        height_ = height;

        RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        AdjustWindowRectEx(&rect, style, FALSE, 0);
        SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message)
        {
            case WM_CLOSE:
                running_ = false;
                return 0;

            case WM_DESTROY:
                running_ = false;
                break;

            case WM_SIZE:
                {
                    const std::uint32_t w = LOWORD(lparam);
                    const std::uint32_t h = HIWORD(lparam);
                    if (w > 0 && h > 0)
                    {
                        width_ = w;
                        height_ = h;
                    }
                    return 0;
                }

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                {
                    const input::Key key = map_virtual_key(wparam);
                    if (key != input::Key::Unknown) input::keyboard.set(key, true);
                    break;
                }

            case WM_KEYUP:
            case WM_SYSKEYUP:
                {
                    const input::Key key = map_virtual_key(wparam);
                    if (key != input::Key::Unknown) input::keyboard.set(key, false);
                    break;
                }

            case WM_KILLFOCUS:
                for (std::size_t i = 0; i < input::kKeyCount; ++i)
                    input::keyboard.set(static_cast<input::Key>(i), false);
                return 0;

            case WM_MOUSEMOVE:
                input::mouse.move_to(static_cast<float>(GET_X_LPARAM(lparam)),
                                     static_cast<float>(GET_Y_LPARAM(lparam)));
                return 0;

            case WM_LBUTTONDOWN: input::mouse.set(input::MouseButton::Left, true); return 0;
            case WM_LBUTTONUP: input::mouse.set(input::MouseButton::Left, false); return 0;
            case WM_RBUTTONDOWN: input::mouse.set(input::MouseButton::Right, true); return 0;
            case WM_RBUTTONUP: input::mouse.set(input::MouseButton::Right, false); return 0;
            case WM_MBUTTONDOWN: input::mouse.set(input::MouseButton::Middle, true); return 0;
            case WM_MBUTTONUP: input::mouse.set(input::MouseButton::Middle, false); return 0;
            case WM_XBUTTONDOWN:
                input::mouse.set(GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? input::MouseButton::X1
                                                                        : input::MouseButton::X2,
                                 true);
                return 0;
            case WM_XBUTTONUP:
                input::mouse.set(GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? input::MouseButton::X1
                                                                        : input::MouseButton::X2,
                                 false);
                return 0;

            case WM_MOUSEWHEEL:
                input::mouse.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                                      static_cast<float>(WHEEL_DELTA);
                return 0;

            default: break;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) return self->handle_message(hwnd, message, wparam, lparam);
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    HWND hwnd_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::string title_;
    bool running_ = false;
    bool owns_hwnd_ = false;
};

std::unique_ptr<Window> Window::create(const WindowDesc& desc)
{
    auto window = std::make_unique<Win32Window>();
    if (!window->create(desc)) return nullptr;
    return window;
}

std::unique_ptr<Window> Window::from_native_handle(void* native, std::uint32_t width,
                                                   std::uint32_t height)
{
    auto window = std::make_unique<Win32Window>();
    if (!window->attach(native, width, height)) return nullptr;
    return window;
}

} // namespace insland::platform

#endif // _WIN32
