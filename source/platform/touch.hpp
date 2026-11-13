#pragma once

#include <cstddef>
#include <cstdint>

namespace insland::input
{

struct TouchPoint
{
    std::int32_t id = -1;
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 0.0f;
    bool down = false;
    bool pressed = false;
    bool released = false;
};

constexpr std::size_t kMaxTouchPoints = 10;

struct TouchState
{
    TouchPoint points[kMaxTouchPoints] = {};
    std::size_t count = 0;

    const TouchPoint* find(std::int32_t id) const
    {
        for (std::size_t i = 0; i < count; ++i)
            if (points[i].id == id) return &points[i];
        return nullptr;
    }

    TouchPoint* find(std::int32_t id)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (points[i].id == id) return &points[i];
        return nullptr;
    }

    void begin(std::int32_t id, float x, float y)
    {
        if (TouchPoint* existing = find(id))
        {
            existing->x = x;
            existing->y = y;
            existing->down = true;
            return;
        }
        if (count >= kMaxTouchPoints) return;

        TouchPoint& p = points[count++];
        p = TouchPoint{};
        p.id = id;
        p.x = x;
        p.y = y;
        p.down = true;
        p.pressed = true;
    }

    void move_to(std::int32_t id, float x, float y)
    {
        if (TouchPoint* p = find(id))
        {
            p->x = x;
            p->y = y;
        }
    }

    void end(std::int32_t id)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (points[i].id != id) continue;
            points[i].down = false;
            points[i].released = true;
            break;
        }
    }

    void end_frame()
    {
        for (std::size_t i = 0; i < count;)
        {
            points[i].pressed = false;
            if (!points[i].down && points[i].released)
            {
                points[i] = points[count - 1];
                points[count - 1] = TouchPoint{};
                --count;
                continue;
            }
            points[i].released = false;
            ++i;
        }
    }
};

inline TouchState touch;

} // namespace insland::input
