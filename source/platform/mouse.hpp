#pragma once

#include <cstddef>
#include <cstdint>

namespace insland::input
{

enum class MouseButton : std::uint8_t
{
    Left = 0,
    Right,
    Middle,
    X1,
    X2,
    Count
};

constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

struct MouseState
{
    float x = 0.0f;
    float y = 0.0f;
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    float wheel = 0.0f;

    bool down[kMouseButtonCount] = {};
    bool pressed[kMouseButtonCount] = {};
    bool released[kMouseButtonCount] = {};

    bool is_down(MouseButton button) const
    {
        return down[static_cast<std::size_t>(button)];
    }

    bool was_pressed(MouseButton button) const
    {
        return pressed[static_cast<std::size_t>(button)];
    }

    bool was_released(MouseButton button) const
    {
        return released[static_cast<std::size_t>(button)];
    }

    void set(MouseButton button, bool state)
    {
        const auto i = static_cast<std::size_t>(button);
        if (state && !down[i]) pressed[i] = true;
        if (!state && down[i]) released[i] = true;
        down[i] = state;
    }

    void move_to(float nx, float ny)
    {
        delta_x += nx - x;
        delta_y += ny - y;
        x = nx;
        y = ny;
    }

    void end_frame()
    {
        delta_x = 0.0f;
        delta_y = 0.0f;
        wheel = 0.0f;
        for (std::size_t i = 0; i < kMouseButtonCount; ++i)
        {
            pressed[i] = false;
            released[i] = false;
        }
    }
};

inline MouseState mouse;

inline bool mouse_down(MouseButton button = MouseButton::Left) { return mouse.is_down(button); }
inline bool mouse_pressed(MouseButton button = MouseButton::Left) { return mouse.was_pressed(button); }
inline bool mouse_released(MouseButton button = MouseButton::Left) { return mouse.was_released(button); }

} // namespace insland::input
