#include "platform/window.hpp"

#if defined(__ANDROID__)

#    include <android/input.h>
#    include <android/native_window.h>
#    include <android_native_app_glue.h>

#    include <cmath>
#    include <cstddef>
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

// ---------------------------------------------------------------------------
// Touch input
// ---------------------------------------------------------------------------
// android_native_app_glue calls app->onInputEvent from inside ALooper_pollOnce,
// i.e. from Window::poll_events() on the same thread that runs the game loop,
// so the global input state below needs no locking.
//
// Every finger is mirrored into input::touch (multi-touch).  The *first* finger
// that lands also drives input::mouse (left button + cursor), which lets the
// desktop camera/picking code work on a phone without changes.  A second finger
// turns the gesture into a pinch that feeds input::mouse.wheel.
namespace
{

constexpr float kPinchPixelsPerNotch = 25.0f; // finger distance change == one wheel notch

std::int32_t g_primary_pointer = -1; // id of the finger emulating the mouse, -1 = none
float g_pinch_distance = -1.0f;      // finger distance at the previous step, -1 = no baseline

std::size_t active_touch_count()
{
    std::size_t active = 0;
    for (std::size_t i = 0; i < input::touch.count; ++i)
        if (input::touch.points[i].down) ++active;
    return active;
}

// Zoom with two fingers: the change in their distance becomes mouse wheel notches.
void update_pinch()
{
    const input::TouchPoint* first = nullptr;
    const input::TouchPoint* second = nullptr;
    for (std::size_t i = 0; i < input::touch.count; ++i)
    {
        if (!input::touch.points[i].down) continue;
        if (!first) first = &input::touch.points[i];
        else if (!second) second = &input::touch.points[i];
    }

    if (!first || !second || active_touch_count() != 2)
    {
        g_pinch_distance = -1.0f;
        return;
    }

    const float dx = second->x - first->x;
    const float dy = second->y - first->y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (g_pinch_distance >= 0.0f)
        input::mouse.wheel += (distance - g_pinch_distance) / kPinchPixelsPerNotch;
    g_pinch_distance = distance;
}

std::int32_t on_input_event(android_app*, AInputEvent* event)
{
    if (!event || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;

    const std::int32_t action = AMotionEvent_getAction(event);
    const std::int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const std::size_t index = static_cast<std::size_t>(
        (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);

    switch (masked)
    {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
    {
        // Pointer *ids* stay stable while fingers come and go; indices do not.
        const std::int32_t id = AMotionEvent_getPointerId(event, index);
        const float x = AMotionEvent_getX(event, index);
        const float y = AMotionEvent_getY(event, index);
        input::touch.begin(id, x, y);

        if (g_primary_pointer < 0)
        {
            g_primary_pointer = id;
            // Put the cursor under the finger WITHOUT going through move_to(),
            // otherwise the jump from the last position becomes a huge delta
            // and the camera snaps on every new touch.
            input::mouse.x = x;
            input::mouse.y = y;
            input::mouse.set(input::MouseButton::Left, true);
        }
        g_pinch_distance = -1.0f; // new finger: restart the pinch baseline
        return 1;
    }

    case AMOTION_EVENT_ACTION_MOVE:
    {
        // MOVE does not say which finger moved, so update all of them.
        const std::size_t count = AMotionEvent_getPointerCount(event);
        const bool pinching = active_touch_count() >= 2;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::int32_t id = AMotionEvent_getPointerId(event, i);
            const float x = AMotionEvent_getX(event, i);
            const float y = AMotionEvent_getY(event, i);
            input::touch.move_to(id, x, y);

            if (id != g_primary_pointer) continue;
            if (pinching)
            {
                // Track the cursor but produce no delta, so the camera does not
                // orbit while zooming and does not jump once a finger lifts.
                input::mouse.x = x;
                input::mouse.y = y;
            }
            else
            {
                input::mouse.move_to(x, y);
            }
        }
        update_pinch();
        return 1;
    }

    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    {
        const std::int32_t id = AMotionEvent_getPointerId(event, index);
        input::touch.end(id);
        if (id == g_primary_pointer)
        {
            // Other fingers do NOT take over the mouse; the next fresh touch will.
            // That avoids a camera jump when one finger of a pinch is lifted.
            g_primary_pointer = -1;
            input::mouse.set(input::MouseButton::Left, false);
        }
        g_pinch_distance = -1.0f;
        return 1;
    }

    case AMOTION_EVENT_ACTION_CANCEL:
    {
        // The system took the gesture away: release everything, never leave
        // the "mouse button" stuck down.
        for (std::size_t i = 0; i < input::touch.count; ++i)
            input::touch.end(input::touch.points[i].id);
        g_primary_pointer = -1;
        g_pinch_distance = -1.0f;
        input::mouse.set(input::MouseButton::Left, false);
        return 1;
    }

    default:
        return 0;
    }
}

} // namespace

void set_android_app(void* app)
{
    g_android_app = app;
    if (app) static_cast<::android_app*>(app)->onInputEvent = on_input_event;
}
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