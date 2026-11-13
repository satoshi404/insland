#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - ray casting
// ---------------------------------------------------------------------------
// CPU ray utilities used by picking, procedural placement and gameplay:
//
//     raycast::Ray ray = raycast::screen_ray(camera, mouse_x, mouse_y, w, h);
//     raycast::Hit hit = raycast::intersect_heightmap(ray, 200.0f, world_height);
//     if (hit.hit) spawn_at(hit.point);
//
// The implementations are deliberately small and allocation free so they are
// safe to call per frame (and inside fixed simulation steps).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <vector>

#include "core/types.hpp"

namespace insland
{

class Camera;

namespace raycast
{

struct Ray
{
    Vec3 origin{0.0f, 0.0f, 0.0f};
    Vec3 direction{0.0f, 0.0f, -1.0f};

    Vec3 at(float t) const { return origin + direction * t; }
};

struct Hit
{
    bool hit = false;
    float distance = 0.0f;
    Vec3 point{0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
};

/// Builds a normalized ray from a camera and screen coordinates (pixels,
/// origin at the top-left corner, +Y down).  `zero_to_one_ndc` selects the
/// projection convention of the backend (D3D vs GL).
Ray screen_ray(const Camera& camera, float x, float y, float width, float height,
               bool zero_to_one_ndc = false);

/// Slab test.  Returns true when the ray hits the box; tmin/tmax delimit the
/// overlapping interval along the ray.
bool intersect_aabb(const Ray& ray, const Bounds& bounds, float& tmin, float& tmax);

/// Möller-Trumbore, two sided.
bool intersect_triangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c,
                        float& t, Vec3& normal);

/// Brute-force triangle test against a vertex/index list.
Hit intersect_mesh(const Ray& ray, const std::vector<Vec3>& positions,
                   const std::vector<std::uint32_t>& indices, float max_distance = 1000.0f);

/// Marches the ray through a heightfield sampled by `height(x, z)`.
/// A final binary refinement gives sub-step precision.
Hit intersect_heightmap(const Ray& ray, float max_distance,
                        const std::function<float(float, float)>& height,
                        float step = 0.5f);

} // namespace raycast
} // namespace insland
