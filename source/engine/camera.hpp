#pragma once

#include <cmath>

#include "core/types.hpp"

namespace insland
{

class Camera
{
public:
    Vec3 position{3.5f, 1.5f, 0.0f};
    Vec3 target{0.0f, 0.5f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};

    float fov_degrees = 60.0f;
    float near_plane = 0.05f;
    float far_plane = 2000.0f;

    // D3D11 uses [0, 1] clip depth, OpenGL uses [-1, 1].  The engine sets this
    // automatically based on the active backend.
    bool zero_to_one_ndc = false;

    Camera() = default;

    Mat4 view() const
    {
        return glm::lookAtRH(position, target, up);
    }

    Mat4 projection(float aspect) const
    {
        if (aspect <= 0.0f) aspect = 1.0f;

        const float tan_half_fov = std::tan(radians(fov_degrees) * 0.5f);
        Mat4 result(0.0f);

        result[0][0] = 1.0f / (aspect * tan_half_fov);
        result[1][1] = 1.0f / tan_half_fov;
        result[2][3] = -1.0f;

        if (zero_to_one_ndc)
        {
            result[2][2] = far_plane / (near_plane - far_plane);
            result[3][2] = (near_plane * far_plane) / (near_plane - far_plane);
        }
        else
        {
            result[2][2] = (far_plane + near_plane) / (near_plane - far_plane);
            result[3][2] = (2.0f * near_plane * far_plane) / (near_plane - far_plane);
        }

        return result;
    }

    Mat4 view_projection(float aspect) const
    {
        return projection(aspect) * view();
    }

    void look_at(const Vec3& point)
    {
        target = point;
    }

    void orbit(float yaw_degrees, float pitch_degrees, float distance, const Vec3& center)
    {
        const float yaw = radians(yaw_degrees);
        const float pitch = radians(clampf(pitch_degrees, -89.0f, 89.0f));

        const Vec3 offset{
            distance * std::cos(pitch) * std::sin(yaw),
            distance * std::sin(pitch),
            distance * std::cos(pitch) * std::cos(yaw)};

        target = center;
        position = center + offset;
        up = Vec3(0.0f, 1.0f, 0.0f);
    }

    Vec3 forward() const { return glm::normalize(target - position); }
    Vec3 right() const { return glm::normalize(glm::cross(forward(), up)); }

    void translate_local(float forward_amount, float right_amount, float up_amount)
    {
        const Vec3 f = forward();
        const Vec3 r = right();
        position += f * forward_amount + r * right_amount + up * up_amount;
        target += f * forward_amount + r * right_amount + up * up_amount;
    }
};

} // namespace insland
