// ---------------------------------------------------------------------------
// Insland Engine - procedural world implementation
// ---------------------------------------------------------------------------

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

#include "world/world.hpp"

#include "core/logger.hpp"
#include "core/memory.hpp"

#include <cmath>
#include <random>

namespace insland
{

namespace
{

// Smooth island mask: 1 in the middle, 0 at the border.
float island_mask(float x, float z, float half_size)
{
    const float r = std::sqrt(x * x + z * z) / half_size;
    const float t = clampf((1.0f - r) / 0.45f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec4 mix_color(const Vec4& a, const Vec4& b, float t)
{
    return a + (b - a) * clampf(t, 0.0f, 1.0f);
}

// Builds one stylised tree in local space (root at origin).
MeshData build_tree_mesh()
{
    MeshData mesh;

    auto add_box = [&](const Vec3& center, const Vec3& half, const Vec4& color) {
        const Vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int face = 0; face < 6; ++face)
        {
            const Vec3 n = normals[face];
            Vec3 up = std::abs(n.y) > 0.5f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
            const Vec3 right = glm::normalize(glm::cross(up, n));
            up = glm::normalize(glm::cross(n, right));

            const Vec3 c = center + n * half;
            const Vec3 corners[4] = {
                c - right * half - up * half,
                c + right * half - up * half,
                c + right * half + up * half,
                c - right * half + up * half};

            const std::uint32_t base = static_cast<std::uint32_t>(mesh.positions.size());
            for (const Vec3& corner : corners)
            {
                mesh.positions.push_back(corner);
                mesh.normals.push_back(n);
                mesh.uvs.push_back(Vec2(0.0f, 0.0f));
                mesh.colors.push_back(color);
            }
            mesh.indices.insert(mesh.indices.end(),
                                {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        }
    };

    // Trunk.
    add_box(Vec3(0.0f, 0.9f, 0.0f), Vec3(0.16f, 0.9f, 0.16f), Vec4(0.28f, 0.19f, 0.12f, 1.0f));

    // Three stacked foliage boxes with decreasing size.
    add_box(Vec3(0.0f, 2.05f, 0.0f), Vec3(0.95f, 0.55f, 0.95f), Vec4(0.13f, 0.32f, 0.14f, 1.0f));
    add_box(Vec3(0.0f, 2.95f, 0.0f), Vec3(0.72f, 0.48f, 0.72f), Vec4(0.16f, 0.38f, 0.16f, 1.0f));
    add_box(Vec3(0.0f, 3.72f, 0.0f), Vec3(0.48f, 0.4f, 0.48f), Vec4(0.19f, 0.44f, 0.19f, 1.0f));

    mesh.tint = Color::white();
    return mesh;
}

} // namespace

float World::sample_noise(float x, float z) const
{
    const float offset = static_cast<float>(config_.seed % 1024) * 0.731f;
    const float s = config_.noise_scale;

    float h = stb_perlin_fbm_noise3(x * s + offset, z * s - offset, offset, 2.0f, 0.5f, 5);
    h = h * 0.5f + 0.5f;
    h = std::pow(clampf(h, 0.0f, 1.0f), 1.5f);

    // Ridged detail for the mountain tops.
    const float ridge = 1.0f - std::abs(stb_perlin_noise3(x * s * 3.1f, z * s * 3.1f, offset, -1, -1, -1));
    h += ridge * ridge * 0.18f;

    const float mask = island_mask(x, z, config_.size * 0.5f);
    return h * mask * config_.height_scale;
}

bool World::generate(Engine& engine, const WorldConfig& config)
{
    config_ = config;
    config_.resolution = std::max(2, config_.resolution);

    const int cells = config_.resolution;
    const int verts = cells + 1;
    const float half = config_.size * 0.5f;
    const float step = config_.size / static_cast<float>(cells);

    // Height grid ---------------------------------------------------------
    heights_.assign(static_cast<std::size_t>(verts) * static_cast<std::size_t>(verts), 0.0f);
    for (int iz = 0; iz < verts; ++iz)
    {
        const float z = -half + step * static_cast<float>(iz);
        for (int ix = 0; ix < verts; ++ix)
        {
            const float x = -half + step * static_cast<float>(ix);
            heights_[static_cast<std::size_t>(iz) * verts + ix] = sample_noise(x, z);
        }
    }

    auto height = [&](int ix, int iz) -> float {
        ix = std::max(0, std::min(verts - 1, ix));
        iz = std::max(0, std::min(verts - 1, iz));
        return heights_[static_cast<std::size_t>(iz) * verts + ix];
    };

    // Terrain mesh --------------------------------------------------------
    terrain_ = MeshData{};
    terrain_.positions.reserve(static_cast<std::size_t>(verts) * verts);
    terrain_.normals.reserve(static_cast<std::size_t>(verts) * verts);
    terrain_.colors.reserve(static_cast<std::size_t>(verts) * verts);

    const Vec4 sand(0.66f, 0.60f, 0.42f, 1.0f);
    const Vec4 grass(0.20f, 0.38f, 0.17f, 1.0f);
    const Vec4 grass_dark(0.14f, 0.30f, 0.13f, 1.0f);
    const Vec4 rock(0.34f, 0.33f, 0.31f, 1.0f);
    const Vec4 snow(0.86f, 0.89f, 0.93f, 1.0f);

    for (int iz = 0; iz < verts; ++iz)
    {
        const float z = -half + step * static_cast<float>(iz);
        for (int ix = 0; ix < verts; ++ix)
        {
            const float x = -half + step * static_cast<float>(ix);
            const float y = height(ix, iz);

            const float dx = (height(ix + 1, iz) - height(ix - 1, iz)) / (2.0f * step);
            const float dz = (height(ix, iz + 1) - height(ix, iz - 1)) / (2.0f * step);
            const Vec3 normal = glm::normalize(Vec3(-dx, 1.0f, -dz));

            const float slope = 1.0f - normal.y;

            Vec4 color = mix_color(grass_dark, grass, clampf(y / (config_.height_scale * 0.55f), 0.0f, 1.0f));
            color = mix_color(color, rock, clampf((slope - 0.22f) * 3.4f, 0.0f, 1.0f));
            color = mix_color(color, snow, clampf((y - config_.height_scale * 0.72f) * 0.4f, 0.0f, 1.0f));
            color = mix_color(sand, color, clampf((y - config_.water_level) * 1.4f, 0.0f, 1.0f));

            terrain_.positions.emplace_back(x, y, z);
            terrain_.normals.push_back(normal);
            terrain_.uvs.emplace_back(static_cast<float>(ix) / cells, static_cast<float>(iz) / cells);
            terrain_.colors.push_back(color);
        }
    }

    terrain_.indices.reserve(static_cast<std::size_t>(cells) * cells * 6);
    for (int iz = 0; iz < cells; ++iz)
    {
        for (int ix = 0; ix < cells; ++ix)
        {
            const std::uint32_t i0 = static_cast<std::uint32_t>(iz * verts + ix);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(verts);
            const std::uint32_t i3 = i2 + 1;

            terrain_.indices.insert(terrain_.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    Memory::add(Memory::Category::Terrain,
                terrain_.positions.size() * sizeof(Vec3) + terrain_.normals.size() * sizeof(Vec3) +
                    terrain_.colors.size() * sizeof(Vec4) + terrain_.indices.size() * sizeof(std::uint32_t));

    terrain_model_ = engine.create_model(terrain_);
    if (!terrain_model_.valid())
    {
        Log::Error("World", "failed to upload terrain mesh");
        return false;
    }

    // Water ---------------------------------------------------------------
    if (config_.build_water)
    {
        water_ = MeshData{};
        const float w = half * 2.6f;
        const float y = config_.water_level;
        water_.positions = {{-w, y, -w}, {w, y, -w}, {w, y, w}, {-w, y, w}};
        water_.normals = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
        water_.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        water_.colors = {{0.08f, 0.24f, 0.38f, 1.0f}, {0.08f, 0.24f, 0.38f, 1.0f},
                         {0.10f, 0.30f, 0.46f, 1.0f}, {0.10f, 0.30f, 0.46f, 1.0f}};
        water_.indices = {0, 2, 1, 0, 3, 2};

        Memory::add(Memory::Category::Terrain, water_.positions.size() * sizeof(Vec3) +
                                                   water_.indices.size() * sizeof(std::uint32_t));
        water_model_ = engine.create_model(water_);
    }

    // Trees ---------------------------------------------------------------
    tree_transforms_.clear();
    if (config_.build_trees)
    {
        tree_model_ = engine.create_model(build_tree_mesh());
        if (tree_model_.valid())
        {
            std::mt19937 rng(config_.seed);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);

            const float min_height = config_.water_level + 0.9f;
            int attempts = 0;
            while (static_cast<int>(tree_transforms_.size()) < config_.tree_count &&
                   attempts < config_.tree_count * 30)
            {
                ++attempts;
                const float x = (unit(rng) - 0.5f) * config_.size * 0.92f;
                const float z = (unit(rng) - 0.5f) * config_.size * 0.92f;
                const float y = height_at(x, z);
                if (y < min_height) continue;

                const Vec3 normal = normal_at(x, z);
                if (normal.y < 0.86f) continue; // too steep for a tree

                Transform transform;
                transform.position = Vec3(x, y - 0.1f, z);
                transform.rotation = glm::angleAxis(unit(rng) * TWO_PI, Vec3(0.0f, 1.0f, 0.0f));
                const float scale = 0.7f + unit(rng) * 0.9f;
                transform.scale = Vec3(scale);
                tree_transforms_.push_back(transform);
            }
        }
    }

    Log::Info("World", "generated island seed=", config_.seed, " cells=", cells, " trees=",
              tree_transforms_.size(), " water=", config_.build_water ? "on" : "off");
    return true;
}

void World::draw(Engine& engine) const
{
    if (terrain_model_.valid()) engine.draw(terrain_model_);
    if (water_model_.valid()) engine.draw(water_model_);
    if (tree_model_.valid())
        for (const Transform& transform : tree_transforms_) engine.draw(tree_model_, transform);
}

float World::height_at(float x, float z) const
{
    const int verts = config_.resolution + 1;
    if (heights_.empty() || verts < 2) return 0.0f;

    const float half = config_.size * 0.5f;
    const float step = config_.size / static_cast<float>(config_.resolution);

    const float fx = clampf((x + half) / step, 0.0f, static_cast<float>(config_.resolution) - 0.0001f);
    const float fz = clampf((z + half) / step, 0.0f, static_cast<float>(config_.resolution) - 0.0001f);

    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float tx = fx - static_cast<float>(ix);
    const float tz = fz - static_cast<float>(iz);

    auto sample = [&](int sx, int sz) {
        sx = std::max(0, std::min(verts - 1, sx));
        sz = std::max(0, std::min(verts - 1, sz));
        return heights_[static_cast<std::size_t>(sz) * verts + sx];
    };

    const float h00 = sample(ix, iz);
    const float h10 = sample(ix + 1, iz);
    const float h01 = sample(ix, iz + 1);
    const float h11 = sample(ix + 1, iz + 1);

    return lerpf(lerpf(h00, h10, tx), lerpf(h01, h11, tx), tz);
}

Vec3 World::normal_at(float x, float z) const
{
    constexpr float e = 0.35f;
    const float hx = height_at(x + e, z) - height_at(x - e, z);
    const float hz = height_at(x, z + e) - height_at(x, z - e);
    return glm::normalize(Vec3(-hx, 2.0f * e, -hz));
}

} // namespace insland
