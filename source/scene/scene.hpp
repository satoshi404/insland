#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - serializable scene
// ---------------------------------------------------------------------------
// A Scene is a small, data-driven description of what to draw:
//
//     insland::Scene scene;
//     if (scene.load("assets/island.scene.json"))
//     {
//         scene.generate_world(engine);
//         scene.draw(engine);
//     }
//
// The same object can be filled from code and written back:
//
//     SceneEntity& cube = scene.add("Cube", "", Transform{});
//     cube.color = Color::red();
//     scene.save("assets/island.scene.json");
//
// Models are referenced by path and loaded lazily on the first draw; repeated
// entities share the same GPU model.  The JSON format is intentionally plain
// so it can be edited by hand or produced by tools.  See serializer.hpp for
// the document envelope (format/version header, atomic writes).
// ---------------------------------------------------------------------------

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/engine.hpp"
#include "world/world.hpp"

namespace insland
{

struct SceneEntity
{
    std::string name = "Entity";
    std::string model;                 // empty draws a cube
    Transform transform;
    Color color = Color::white();      // used by cubes, kept for model tinting
    bool visible = true;

    // Animation: -1 leaves the model alone, 0..N plays that clip.  The clip is
    // started once, when the model is first drawn.
    int animation = -1;
    bool loop = true;
};

class Scene
{
public:
    std::string name = "Untitled Scene";

    WorldConfig world_config;
    bool world_enabled = true;
    bool sky_enabled = true;

    std::vector<SceneEntity> entities;

    Scene() = default;

    /// Removes every entity and forgets loaded models.
    void clear();

    /// Loads a scene written by save().  Returns false and logs the reason on
    /// malformed input; the scene is left unchanged.
    bool load(const std::string& path);

    /// Writes the scene as JSON.  The write is atomic.
    bool save(const std::string& path) const;

    /// Adds an entity and returns a reference to it (valid until the next
    /// add()/clear()).
    SceneEntity& add(const std::string& entity_name, const std::string& model_path = {},
                     const Transform& transform = {}, const Color& color = Color::white());

    /// Generates the procedural world described by world_config.  Safe to call
    /// more than once; pass force=true to regenerate with new settings.
    bool generate_world(Engine& engine, bool force = false);

    /// Draws the world and every visible entity.  Models are loaded on demand.
    void draw(Engine& engine);

    /// Terrain height at a world position (0 when the world is disabled).
    float height_at(float x, float z) const;

    bool world_ready() const { return world_ready_; }
    const World& world() const { return world_; }
    const std::string& path() const { return path_; }

private:
    Model model_for(Engine& engine, const std::string& path);

    std::string path_;
    World world_;
    bool world_ready_ = false;
    std::unordered_map<std::string, Model> models_;
    std::unordered_set<std::string> animations_started_;
};

} // namespace insland
