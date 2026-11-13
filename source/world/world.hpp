#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - procedural world
// ---------------------------------------------------------------------------
// Generates a small island: an fbm height field built into a MeshData, a water
// plane at sea level and a handful of trees placed on flat, dry ground.
//
//     insland::World world;
//     WorldConfig config;
//     config.seed = 7;
//     world.generate(engine, config);
//     world.draw(engine);
//     raycast::Hit hit = raycast::intersect_heightmap(
//         ray, 400.0f, [&](float x, float z) { return world.height_at(x, z); });
//
// height_at() bilinearly samples the stored grid, so picking and placement use
// exactly the geometry that is drawn.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <vector>

#include "engine/engine.hpp"

namespace insland
{

struct WorldConfig
{
    std::uint32_t seed = 1337;
    int resolution = 128;          // grid quads per side
    float size = 96.0f;            // world extent in metres
    float height_scale = 14.0f;
    float noise_scale = 0.018f;
    float water_level = 2.4f;
    int tree_count = 90;
    bool build_water = true;
    bool build_trees = true;
};

class World
{
public:
    World() = default;

    /// Builds the terrain, water and trees.  The engine must be initialised.
    bool generate(Engine& engine, const WorldConfig& config = {});

    /// Draws terrain, water and every tree.
    void draw(Engine& engine) const;

    /// Bilinearly sampled terrain height.  Returns 0 outside the world bounds.
    float height_at(float x, float z) const;

    /// Surface normal at a world position (central differences).
    Vec3 normal_at(float x, float z) const;

    const WorldConfig& config() const { return config_; }
    const MeshData& terrain_mesh() const { return terrain_; }
    const MeshData& water_mesh() const { return water_; }

    const Model& terrain_model() const { return terrain_model_; }
    const Model& water_model() const { return water_model_; }
    const std::vector<Transform>& tree_transforms() const { return tree_transforms_; }

    bool valid() const { return terrain_model_.valid(); }

private:
    float sample_noise(float x, float z) const;

    WorldConfig config_;
    MeshData terrain_;
    MeshData water_;
    Model terrain_model_;
    Model water_model_;
    Model tree_model_;
    std::vector<Transform> tree_transforms_;
    std::vector<float> heights_;   // (resolution + 1)^2 samples
};

} // namespace insland
