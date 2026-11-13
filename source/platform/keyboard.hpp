#pragma once

#include <cstddef>
#include <cstdint>

namespace insland::input
{

// Canonical key codes.  Printable keys use their ASCII value so they can be
// compared directly (Key::A == 'A').  Non-printable keys start at 256.
enum class Key : std::uint16_t
{
    Unknown = 0,
    Backspace = 8,
    Tab = 9,
    Enter = 13,
    Escape = 27,
    Space = 32,

    Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
    Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',

    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G',
    H = 'H', I = 'I', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N',
    O = 'O', P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U',
    V = 'V', W = 'W', X = 'X', Y = 'Y', Z = 'Z',

    Insert = 256,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    CapsLock,
    NumLock,
    ScrollLock,
    Pause,
    PrintScreen,
    Menu,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadAdd,
    KeypadSubtract,
    KeypadMultiply,
    KeypadDivide,
    KeypadDecimal,
    KeypadEnter,

    Count
};

constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count) + 8;

struct KeyboardState
{
    bool down[kKeyCount] = {};
    bool pressed[kKeyCount] = {};
    bool released[kKeyCount] = {};

    static constexpr std::size_t index(Key key)
    {
        return static_cast<std::size_t>(key) % kKeyCount;
    }

    bool is_down(Key key) const { return down[index(key)]; }
    bool was_pressed(Key key) const { return pressed[index(key)]; }
    bool was_released(Key key) const { return released[index(key)]; }

    void set(Key key, bool state)
    {
        const std::size_t i = index(key);
        if (state && !down[i]) pressed[i] = true;
        if (!state && down[i]) released[i] = true;
        down[i] = state;
    }

    void end_frame()
    {
        for (std::size_t i = 0; i < kKeyCount; ++i)
        {
            pressed[i] = false;
            released[i] = false;
        }
    }
};

// Global input state.  Platform windows write to these objects; game code only
// reads them.  inline variables keep this header-only and avoid an extra .cpp.
inline KeyboardState keyboard;

inline bool key_down(Key key) { return keyboard.is_down(key); }
inline bool key_pressed(Key key) { return keyboard.was_pressed(key); }
inline bool key_released(Key key) { return keyboard.was_released(key); }

inline bool is_alpha(Key key)
{
    const auto value = static_cast<std::uint16_t>(key);
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

inline bool is_digit(Key key)
{
    const auto value = static_cast<std::uint16_t>(key);
    return value >= '0' && value <= '9';
}

} // namespace insland::input
