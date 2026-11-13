#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - basic types and math
// ---------------------------------------------------------------------------
// All math is built on top of glm.  The engine uses a right-handed coordinate
// system (the same convention used by glTF), +Y up, and column-major matrices
// (the native memory layout of glm and HLSL/SQL constant buffers).
// ---------------------------------------------------------------------------

#ifndef GLM_ENABLE_EXPERIMENTAL
#    define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace insland
{
using glm::mat3;
using glm::mat4;
using glm::quat;
using glm::vec2;
using glm::vec3;
using glm::vec4;

using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using UVec4 = glm::uvec4;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = PI * 2.0f;
constexpr float HALF_PI = PI * 0.5f;

inline float radians(float degrees) { return glm::radians(degrees); }
inline float degrees(float radians) { return glm::degrees(radians); }
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------
struct Color
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float rr, float gg, float bb, float aa = 1.0f) : r(rr), g(gg), b(bb), a(aa) {}

    static constexpr Color white() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static constexpr Color black() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color red() { return {1.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color green() { return {0.0f, 1.0f, 0.0f, 1.0f}; }
    static constexpr Color blue() { return {0.0f, 0.0f, 1.0f, 1.0f}; }

    Vec4 to_vec4() const { return {r, g, b, a}; }
    static Color from_vec4(const Vec4& v) { return {v.x, v.y, v.z, v.w}; }
};

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------
struct Transform
{
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
    Vec3 scale{1.0f, 1.0f, 1.0f};

    Transform() = default;
    explicit Transform(const Vec3& pos) : position(pos) {}

    static Transform from_matrix(const Mat4& m)
    {
        Transform t;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(m, t.scale, t.rotation, t.position, skew, perspective);
        return t;
    }

    Mat4 matrix() const
    {
        Mat4 m = glm::translate(Mat4(1.0f), position);
        m *= glm::mat4_cast(rotation);
        m = glm::scale(m, scale);
        return m;
    }

    Mat3 normal_matrix() const
    {
        return glm::transpose(glm::inverse(Mat3(matrix())));
    }

    // Convenience helpers ---------------------------------------------------
    Transform& set_position(float x, float y, float z)
    {
        position = {x, y, z};
        return *this;
    }

    Transform& set_rotation_euler(float x_deg, float y_deg, float z_deg)
    {
        rotation = Quat(radians(Vec3(x_deg, y_deg, z_deg)));
        return *this;
    }

    Transform& rotate_y(float degrees_)
    {
        rotation = glm::angleAxis(radians(degrees_), Vec3(0.0f, 1.0f, 0.0f)) * rotation;
        return *this;
    }

    Transform& set_scale(float uniform_scale)
    {
        scale = {uniform_scale, uniform_scale, uniform_scale};
        return *this;
    }
};

// ---------------------------------------------------------------------------
// Axis-aligned bounding box
// ---------------------------------------------------------------------------
struct Bounds
{
    Vec3 min{std::numeric_limits<float>::max()};
    Vec3 max{std::numeric_limits<float>::lowest()};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    void expand(const Vec3& p)
    {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    void expand(const Bounds& b)
    {
        if (!b.valid()) return;
        min = glm::min(min, b.min);
        max = glm::max(max, b.max);
    }

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extent() const { return max - min; }
    Vec3 size() const { return max - min; }
};

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// ---------------------------------------------------------------------------
// Small helpers used by several subsystems
// ---------------------------------------------------------------------------
template <typename T>
constexpr T align_up(T value, T alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T>
constexpr T min_value(T a, T b) { return a < b ? a : b; }

template <typename T>
constexpr T max_value(T a, T b) { return a > b ? a : b; }

inline std::string to_string(const Vec3& v)
{
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

} // namespace insland
