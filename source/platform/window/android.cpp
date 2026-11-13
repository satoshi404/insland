#include "platform/window.hpp"

#if defined(__ANDROID__)

#    include <android/native_window.h>
#    include <android_native_app_glue.h>

#    include <cstdint>
#    include <string>

#    include "platform/android.hpp"

namespace insland::platform
{
namespace
{
void* g_android_app = nullptr;
void* g_android_window = nullptr;
std::string g_android_asset_root;
} // namespace

void set_android_app(void* app) { g_android_app = app; }
void* android_current_app() { return g_android_app; }

void set_android_native_window(void* window) { g_android_window = window; }
void* android_native_window() { return g_android_window; }

void set_android_asset_root(const std::string& root) { g_android_asset_root = root; }
const std::string& android_asset_root() { return g_android_asset_root; }

// On Android the window is owned by the activity.  Game code obtains the
// ANativeWindow from the Java/Kotlin side and passes it through
// Engine::Config::native_window.  Touch events are delivered by the activity
// as well and can be written into insland::input::touch.
class AndroidWindow final : public Window
{
public:
    ~AndroidWindow() override = default;

    bool attach(void* native, std::uint32_t width, std::uint32_t height)
    {
        window_ = static_cast<ANativeWindow*>(native);
        if (!window_) return false;
        width_ = width ? width : static_cast<std::uint32_t>(ANativeWindow_getWidth(window_));
        height_ = height ? height : static_cast<std::uint32_t>(ANativeWindow_getHeight(window_));
        running_ = true;
        return true;
    }

    bool poll_events() override
    {
        if (auto* app = static_cast<::android_app*>(android_current_app()))
        {
            int events = 0;
            android_poll_source* source = nullptr;
            while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
            {
                if (source) source->process(app, source);
                if (app->destroyRequested) break;
            }

            // Losing the window means the activity went to the background.
            // Stop the render loop so the engine shuts down cleanly.
            if (app->destroyRequested || !app->window)
            {
                running_ = false;
                return false;
            }

            window_ = app->window;
            width_ = static_cast<std::uint32_t>(ANativeWindow_getWidth(window_));
            height_ = static_cast<std::uint32_t>(ANativeWindow_getHeight(window_));
        }
        return running_;
    }

    void* native_handle() const override { return window_; }
    void* native_display() const override { return nullptr; }

    std::uint32_t width() const override
    {
        if (window_) return static_cast<std::uint32_t>(ANativeWindow_getWidth(window_));
        return width_;
    }

    std::uint32_t height() const override
    {
        if (window_) return static_cast<std::uint32_t>(ANativeWindow_getHeight(window_));
        return height_;
    }

    bool should_close() const override { return !running_; }

    void set_should_close(bool value = true) override { running_ = !value; }

    void set_title(const std::string&) override {}

    void set_size(std::uint32_t width, std::uint32_t height) override
    {
        width_ = width;
        height_ = height;
    }

private:
    ANativeWindow* window_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool running_ = false;
};

std::unique_ptr<Window> Window::create(const WindowDesc&)
{
    // A window cannot be created outside of the Android activity lifecycle.
    return nullptr;
}

std::unique_ptr<Window> Window::from_native_handle(void* native, std::uint32_t width, std::uint32_t height)
{
    auto window = std::make_unique<AndroidWindow>();
    if (!window->attach(native, width, height)) return nullptr;
    return window;
}

} // namespace insland::platform

#endif // __ANDROID__
