// ---------------------------------------------------------------------------
// Insland Engine - serializable scene implementation
// ---------------------------------------------------------------------------

#include "scene/scene.hpp"

#include "core/logger.hpp"
#include "core/serializer.hpp"

#include <algorithm>

namespace insland
{
namespace
{

JsonValue world_to_json(const WorldConfig& world, bool enabled)
{
    JsonValue out = JsonValue::object();
    out["enabled"] = enabled;
    out["seed"] = world.seed;
    out["resolution"] = world.resolution;
    out["size"] = world.size;
    out["height_scale"] = world.height_scale;
    out["noise_scale"] = world.noise_scale;
    out["water_level"] = world.water_level;
    out["tree_count"] = world.tree_count;
    out["build_water"] = world.build_water;
    out["build_trees"] = world.build_trees;
    return out;
}

void world_from_json(const JsonValue& value, WorldConfig& world, bool& enabled)
{
    if (!value.is_object()) return;
    enabled = value["enabled"].as_bool(enabled);
    world.seed = value["seed"].as_u32(world.seed);
    world.resolution = value["resolution"].as_int(world.resolution);
    world.size = value["size"].as_float(world.size);
    world.height_scale = value["height_scale"].as_float(world.height_scale);
    world.noise_scale = value["noise_scale"].as_float(world.noise_scale);
    world.water_level = value["water_level"].as_float(world.water_level);
    world.tree_count = value["tree_count"].as_int(world.tree_count);
    world.build_water = value["build_water"].as_bool(world.build_water);
    world.build_trees = value["build_trees"].as_bool(world.build_trees);

    // Keep the generator inside a sane range even if the file was edited by
    // hand.  This avoids allocating enormous grids by accident.
    world.resolution = std::max(4, std::min(world.resolution, 512));
    world.tree_count = std::max(0, std::min(world.tree_count, 2000));
    world.size = std::max(1.0f, world.size);
}

} // namespace

void Scene::clear()
{
    entities.clear();
    models_.clear();
    animations_started_.clear();
    world_ready_ = false;
    path_.clear();
}

bool Scene::load(const std::string& path)
{
    std::string error;
    auto document = load_document(path, &error);
    if (!document)
    {
        Log::Error("Scene", error);
        return false;
    }

    const JsonValue& root = *document;

    Scene loaded;
    loaded.name = root["name"].as_string("Untitled Scene");
    loaded.sky_enabled = root["sky"].as_bool(true);

    if (const JsonValue* world = root.find("world"))
        world_from_json(*world, loaded.world_config, loaded.world_enabled);

    if (const JsonValue* list = root.find("entities"))
    {
        if (!list->is_array())
        {
            Log::Error("Scene", path, ": 'entities' must be an array");
            return false;
        }

        for (const JsonValue& item : list->items())
        {
            if (!item.is_object())
            {
                Log::Warn("Scene", path, ": skipping a non-object entity");
                continue;
            }

            SceneEntity entity;
            entity.name = item["name"].as_string("Entity");
            entity.model = item["model"].as_string();
            entity.transform = transform_from_json(item["transform"], Transform{});
            entity.color = color_from_json(item["color"], Color::white());
            entity.visible = item["visible"].as_bool(true);
            entity.animation = item["animation"].as_int(-1);
            entity.loop = item["loop"].as_bool(true);
            loaded.entities.push_back(std::move(entity));
        }
    }

    loaded.path_ = path;
    *this = std::move(loaded);
    Log::Info("Scene", "loaded '", name, "' (", entities.size(), " entities) from ", path);
    return true;
}

bool Scene::save(const std::string& path) const
{
    JsonValue document = JsonValue::object();
    document["name"] = name;
    document["sky"] = sky_enabled;
    document["world"] = world_to_json(world_config, world_enabled);

    JsonValue list = JsonValue::array();
    for (const SceneEntity& entity : entities)
    {
        JsonValue item = JsonValue::object();
        item["name"] = entity.name;
        if (!entity.model.empty()) item["model"] = entity.model;
        item["transform"] = to_json(entity.transform);
        item["color"] = to_json(entity.color);
        item["visible"] = entity.visible;
        item["animation"] = entity.animation;
        item["loop"] = entity.loop;
        list.push(std::move(item));
    }
    document["entities"] = std::move(list);

    std::string error;
    if (!save_document(path, std::move(document), &error))
    {
        Log::Error("Scene", error);
        return false;
    }

    Log::Info("Scene", "saved '", name, "' (", entities.size(), " entities) to ", path);
    return true;
}

SceneEntity& Scene::add(const std::string& entity_name, const std::string& model_path,
                        const Transform& transform, const Color& color)
{
    SceneEntity entity;
    entity.name = entity_name;
    entity.model = model_path;
    entity.transform = transform;
    entity.color = color;
    entities.push_back(std::move(entity));
    return entities.back();
}

bool Scene::generate_world(Engine& engine, bool force)
{
    if (!world_enabled)
    {
        world_ready_ = false;
        return true;
    }

    if (world_ready_ && !force) return true;

    world_ready_ = world_.generate(engine, world_config);
    return world_ready_;
}

Model Scene::model_for(Engine& engine, const std::string& path)
{
    const auto existing = models_.find(path);
    if (existing != models_.end()) return existing->second;

    Model model = engine.load_model(GLTF, path);
    if (!model.valid()) Log::Warn("Scene", "could not load model '", path, "'");

    models_.emplace(path, model);
    return model;
}

void Scene::draw(Engine& engine)
{
    if (world_enabled && world_ready_) world_.draw(engine);

    for (const SceneEntity& entity : entities)
    {
        if (!entity.visible) continue;

        if (entity.model.empty())
        {
            engine.draw_cube(entity.transform, entity.color);
            continue;
        }

        Model model = model_for(engine, entity.model);
        if (!model.valid()) continue;

        if (entity.animation >= 0 && animations_started_.insert(entity.model).second)
        {
            const int clip = entity.animation;
            if (clip >= 0 && static_cast<std::size_t>(clip) < model.animation_count())
                model.play(static_cast<std::size_t>(clip), entity.loop);
        }

        engine.draw(model, entity.transform);
    }
}

float Scene::height_at(float x, float z) const
{
    return world_ready_ ? world_.height_at(x, z) : 0.0f;
}

} // namespace insland
