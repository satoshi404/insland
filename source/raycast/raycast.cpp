// ---------------------------------------------------------------------------
// Insland Engine - ray casting implementation
// ---------------------------------------------------------------------------

#include "raycast/raycast.hpp"

#include "engine/camera.hpp"

#include <algorithm>
#include <cmath>

namespace insland::raycast
{

Ray screen_ray(const Camera& camera, float x, float y, float width, float height,
               bool zero_to_one_ndc)
{
    Ray ray;
    if (width <= 0.0f || height <= 0.0f) return ray;

    const Mat4 inverse = glm::inverse(camera.view_projection(width / height));

    const float ndc_x = (x / width) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (y / height) * 2.0f;
    const float z_near = zero_to_one_ndc ? 0.0f : -1.0f;

    const Vec4 near_h = inverse * Vec4(ndc_x, ndc_y, z_near, 1.0f);
    const Vec4 far_h = inverse * Vec4(ndc_x, ndc_y, 1.0f, 1.0f);
    if (std::abs(near_h.w) < 1e-6f || std::abs(far_h.w) < 1e-6f) return ray;

    const Vec3 near_point = Vec3(near_h) / near_h.w;
    const Vec3 far_point = Vec3(far_h) / far_h.w;

    ray.origin = near_point;
    ray.direction = glm::normalize(far_point - near_point);
    return ray;
}

bool intersect_aabb(const Ray& ray, const Bounds& bounds, float& tmin, float& tmax)
{
    tmin = 0.0f;
    tmax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        const float lo = bounds.min[axis];
        const float hi = bounds.max[axis];

        if (std::abs(direction) < 1e-8f)
        {
            if (origin < lo || origin > hi) return false;
            continue;
        }

        const float inv = 1.0f / direction;
        float t0 = (lo - origin) * inv;
        float t1 = (hi - origin) * inv;
        if (t0 > t1) std::swap(t0, t1);

        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmin > tmax) return false;
    }
    return true;
}

bool intersect_triangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c,
                        float& t, Vec3& normal)
{
    constexpr float kEpsilon = 1e-7f;

    const Vec3 edge1 = b - a;
    const Vec3 edge2 = c - a;
    const Vec3 p = glm::cross(ray.direction, edge2);
    const float determinant = glm::dot(edge1, p);

    // Two sided: no back-face culling, which keeps picking robust for open or
    // inverted meshes.
    if (std::abs(determinant) < kEpsilon) return false;

    const float inv_det = 1.0f / determinant;
    const Vec3 s = ray.origin - a;
    const float u = glm::dot(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;

    const Vec3 q = glm::cross(s, edge1);
    const float v = glm::dot(ray.direction, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;

    t = glm::dot(edge2, q) * inv_det;
    if (t <= kEpsilon) return false;

    normal = glm::normalize(glm::cross(edge1, edge2));
    return true;
}

Hit intersect_mesh(const Ray& ray, const std::vector<Vec3>& positions,
                   const std::vector<std::uint32_t>& indices, float max_distance)
{
    Hit result;
    if (positions.empty() || indices.size() < 3) return result;

    float closest = max_distance;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const std::uint32_t i0 = indices[i + 0];
        const std::uint32_t i1 = indices[i + 1];
        const std::uint32_t i2 = indices[i + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;

        float t = 0.0f;
        Vec3 normal{0.0f, 1.0f, 0.0f};
        if (!intersect_triangle(ray, positions[i0], positions[i1], positions[i2], t, normal)) continue;
        if (t >= closest) continue;

        closest = t;
        result.hit = true;
        result.distance = t;
        result.point = ray.at(t);
        result.normal = normal;
    }
    return result;
}

Hit intersect_heightmap(const Ray& ray, float max_distance,
                        const std::function<float(float, float)>& height, float step)
{
    Hit result;
    if (!height || step <= 0.0f) return result;

    auto ground = [&](const Vec3& p) { return height(p.x, p.z); };
    auto above = [&](const Vec3& p) { return p.y - ground(p); };

    float previous_t = 0.0f;
    Vec3 previous = ray.origin;
    float previous_diff = above(previous);
    if (previous_diff <= 0.0f)
    {
        // Starting inside the terrain: report an immediate hit.
        result.hit = true;
        result.distance = 0.0f;
        result.point = previous;
        result.normal = Vec3(0.0f, 1.0f, 0.0f);
        return result;
    }

    for (float t = step; t <= max_distance; t += step)
    {
        const Vec3 point = ray.at(t);
        const float diff = above(point);
        if (diff <= 0.0f)
        {
            // Bisect between the last point above and this one below.
            float lo = previous_t;
            float hi = t;
            for (int i = 0; i < 20; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                if (above(ray.at(mid)) > 0.0f)
                    lo = mid;
                else
                    hi = mid;
            }

            result.hit = true;
            result.distance = 0.5f * (lo + hi);
            result.point = ray.at(result.distance);

            // Normal from central differences of the height field.
            constexpr float e = 0.5f;
            const float hx = height(result.point.x + e, result.point.z) -
                             height(result.point.x - e, result.point.z);
            const float hz = height(result.point.x, result.point.z + e) -
                             height(result.point.x, result.point.z - e);
            result.normal = glm::normalize(Vec3(-hx, 2.0f * e, -hz));
            return result;
        }

        previous = point;
        previous_t = t;
        previous_diff = diff;
    }

    (void)previous_diff;
    return result;
}

} // namespace insland::raycast
