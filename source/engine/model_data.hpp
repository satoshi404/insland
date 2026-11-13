#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - internal model representation
// ---------------------------------------------------------------------------
// This header is intentionally private to the implementation.  Game code only
// sees the small Model handle declared in engine/model.hpp.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "Buffer.h"
#include "Texture.h"
#include "TextureView.h"
#include "Sampler.h"

#include "core/types.hpp"
#include "pipeline.hpp"

namespace insland::detail
{

struct Material
{
    std::string name;
    Color base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    bool double_sided = false;
    bool alpha_blend = false;

    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> texture_view;
    Diligent::RefCntAutoPtr<Diligent::ISampler> sampler;
};

struct GpuMesh
{
    std::string name;
    std::vector<u32> indices;
    Bounds bounds;
    int material_index = -1;
    u32 index_count = 0;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertex_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> index_buffer;
};

struct Node
{
    std::string name;
    int parent = -1;
    std::vector<int> children;

    int mesh = -1;
    int skin = -1;

    Vec3 translation{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f};
    Mat4 static_local{1.0f};
    Mat4 static_global{1.0f};

    bool animated = false;
};

struct Skin
{
    std::vector<int> joints;
    std::vector<Mat4> inverse_bind_matrices;
};

struct AnimationChannel
{
    enum class Path
    {
        Translation,
        Rotation,
        Scale
    };

    enum class Interpolation
    {
        Linear,
        Step,
        CubicSpline
    };

    int node = -1;
    Path path = Path::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times;
    std::vector<float> values; // 3 floats for T/S, 4 floats for R; cubic spline stores 3 values per key
};

struct Animation
{
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
};

struct Instance
{
    u32 mesh = 0;
    int node = -1;
    int skin = -1;
    Mat4 static_world{1.0f};

    // Runtime data, refreshed by ModelData::evaluate().
    Mat4 world{1.0f};
    std::vector<Mat4> bones;
};

struct ModelData
{
    std::string name;
    std::vector<GpuMesh> meshes;
    std::vector<Material> materials;
    mutable std::vector<Instance> instances;
    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<Animation> animations;
    std::vector<int> roots;
    Bounds bounds;

    int current_animation = -1;
    float animation_time = 0.0f;
    float animation_speed = 1.0f;
    bool playing = false;
    bool loop = true;

    mutable std::vector<Mat4> node_globals;
    mutable bool needs_evaluation = true;

    void play(int animation_index = 0, bool should_loop = true)
    {
        if (animation_index < 0 || animation_index >= static_cast<int>(animations.size())) return;
        current_animation = animation_index;
        animation_time = 0.0f;
        playing = true;
        loop = should_loop;
        needs_evaluation = true;
    }

    void stop()
    {
        playing = false;
        animation_time = 0.0f;
        needs_evaluation = true;
    }

    void update(float dt)
    {
        if (!playing || current_animation < 0) return;
        const Animation& animation = animations[static_cast<std::size_t>(current_animation)];
        animation_time += dt * animation_speed;

        if (animation.duration > 0.0f)
        {
            if (loop)
            {
                animation_time = std::fmod(animation_time, animation.duration);
                if (animation_time < 0.0f) animation_time += animation.duration;
            }
            else
            {
                animation_time = clampf(animation_time, 0.0f, animation.duration);
            }
        }

        needs_evaluation = true;
    }

    void evaluate() const;
};

} // namespace insland::detail
