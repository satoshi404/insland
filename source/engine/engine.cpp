// ---------------------------------------------------------------------------
// Insland Engine - implementation
// ---------------------------------------------------------------------------
// This translation unit owns the third-party implementations (stb_image and
// tinygltf v3), the glTF loader, the animation runtime and the frame loop.
// ---------------------------------------------------------------------------

#include "engine/engine.hpp"
#include "engine/model_data.hpp"

#include "core/filesystem.hpp"
#include "assets/draco_loader.hpp"
#include "graphics/sky.hpp"
#include "pipeline.hpp"
#include "platform/renderer.hpp"
#include "platform/window.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYGLTF3_IMPLEMENTATION
#include "tiny_gltf_v3.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace insland
{
namespace
{

void log_message(const std::string& message)
{
    std::fprintf(stderr, "[insland] %s\n", message.c_str());
}

std::string tg3_string(const tg3_str& value)
{
    if (!value.data || value.len == 0) return {};
    return std::string(value.data, value.len);
}

bool attribute_is(const tg3_str& key, const char* name)
{
    const std::size_t length = std::strlen(name);
    return key.len == length && key.data && std::memcmp(key.data, name, length) == 0;
}

Mat4 matrix_from_doubles(const double* values)
{
    Mat4 result(1.0f);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result[column][row] = static_cast<float>(values[column * 4 + row]);
    return result;
}

// Number of components in a glTF accessor type.
int accessor_components(int type)
{
    if (type == TG3_TYPE_SCALAR) return 1;
    if (type == TG3_TYPE_VEC2) return 2;
    if (type == TG3_TYPE_VEC3) return 3;
    if (type == TG3_TYPE_VEC4) return 4;
    if (type >= 32 && type < 64)
    {
        const int n = type - 32;
        return n * n;
    }
    return 0;
}

int component_size(int component_type)
{
    switch (component_type)
    {
        case TG3_COMPONENT_TYPE_BYTE:
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
        case TG3_COMPONENT_TYPE_SHORT:
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TG3_COMPONENT_TYPE_INT:
        case TG3_COMPONENT_TYPE_UNSIGNED_INT:
        case TG3_COMPONENT_TYPE_FLOAT: return 4;
        case TG3_COMPONENT_TYPE_DOUBLE: return 8;
        default: return 0;
    }
}

double read_component(const u8* data, int component_type)
{
    switch (component_type)
    {
        case TG3_COMPONENT_TYPE_BYTE:
        {
            i8 value;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<double>(value);
        }
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: return static_cast<double>(*data);
        case TG3_COMPONENT_TYPE_SHORT:
        {
            i16 value;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<double>(value);
        }
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            u16 value;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<double>(value);
        }
        case TG3_COMPONENT_TYPE_INT:
        {
            i32 value;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<double>(value);
        }
        case TG3_COMPONENT_TYPE_UNSIGNED_INT:
        {
            u32 value;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<double>(value);
        }
        case TG3_COMPONENT_TYPE_FLOAT:
        {
            float value;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<double>(value);
        }
        case TG3_COMPONENT_TYPE_DOUBLE:
        {
            double value;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
        default: return 0.0;
    }
}

double normalize_component(double value, int component_type)
{
    switch (component_type)
    {
        case TG3_COMPONENT_TYPE_BYTE: return std::max(value / 127.0, -1.0);
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: return value / 255.0;
        case TG3_COMPONENT_TYPE_SHORT: return std::max(value / 32767.0, -1.0);
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: return value / 65535.0;
        default: return value;
    }
}

// Reads an accessor as a flat array of floats.  Returns false when the
// accessor is invalid or its data is not resident in memory.
bool read_accessor(const tg3_model& model, int accessor_index, std::vector<float>& out, int& components)
{
    out.clear();
    components = 0;

    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors_count)) return false;

    const tg3_accessor& accessor = model.accessors[accessor_index];
    components = accessor_components(accessor.type);
    if (components <= 0 || accessor.count == 0) return false;

    const int comp_size = component_size(accessor.component_type);
    if (comp_size <= 0) return false;

    const std::size_t element_size = static_cast<std::size_t>(components) * static_cast<std::size_t>(comp_size);
    out.resize(static_cast<std::size_t>(accessor.count) * static_cast<std::size_t>(components), 0.0f);

    if (accessor.buffer_view < 0 || accessor.buffer_view >= static_cast<int>(model.buffer_views_count))
        return true; // Valid but implicitly zero-filled (sparse-only accessor).

    const tg3_buffer_view& view = model.buffer_views[accessor.buffer_view];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers_count)) return false;

    const tg3_buffer& buffer = model.buffers[view.buffer];
    if (!buffer.data.data) return false;

    const std::size_t stride = view.byte_stride ? view.byte_stride : element_size;
    const u8* base = buffer.data.data + view.byte_offset + accessor.byte_offset;

    for (std::size_t i = 0; i < static_cast<std::size_t>(accessor.count); ++i)
    {
        const u8* element = base + i * stride;
        for (int c = 0; c < components; ++c)
        {
            double value = read_component(element + static_cast<std::size_t>(c) * comp_size, accessor.component_type);
            if (accessor.normalized) value = normalize_component(value, accessor.component_type);
            out[i * static_cast<std::size_t>(components) + static_cast<std::size_t>(c)] = static_cast<float>(value);
        }
    }
    return true;
}

std::vector<u32> read_indices(const tg3_model& model, int accessor_index, std::size_t vertex_count)
{
    std::vector<u32> indices;
    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors_count))
    {
        indices.resize(vertex_count);
        for (std::size_t i = 0; i < vertex_count; ++i) indices[i] = static_cast<u32>(i);
        return indices;
    }

    const tg3_accessor& accessor = model.accessors[accessor_index];
    if (accessor.count == 0) return indices;

    std::vector<float> values;
    int components = 0;
    if (!read_accessor(model, accessor_index, values, components) || components != 1) return indices;

    indices.reserve(accessor.count);
    for (float value : values) indices.push_back(static_cast<u32>(value));
    return indices;
}

struct DracoRef
{
    bool found = false;
    int buffer_view = -1;
    std::vector<draco_loader::AttributeBinding> bindings;
};

// Walks the generic extension tree of a primitive looking for
// KHR_draco_mesh_compression.
DracoRef find_draco_extension(const tg3_primitive& primitive)
{
    DracoRef ref;
    for (uint32_t i = 0; i < primitive.ext.extensions_count; ++i)
    {
        const tg3_extension& extension = primitive.ext.extensions[i];
        if (!attribute_is(extension.name, "KHR_draco_mesh_compression")) continue;

        ref.found = true;
        if (extension.value.type != TG3_VALUE_OBJECT) return ref;

        for (uint32_t k = 0; k < extension.value.object_count; ++k)
        {
            const tg3_kv_pair& entry = extension.value.object_data[k];
            if (attribute_is(entry.key, "bufferView"))
            {
                if (entry.value.type == TG3_VALUE_INT)
                    ref.buffer_view = static_cast<int>(entry.value.int_val);
            }
            else if (attribute_is(entry.key, "attributes") && entry.value.type == TG3_VALUE_OBJECT)
            {
                for (uint32_t a = 0; a < entry.value.object_count; ++a)
                {
                    const tg3_kv_pair& attribute = entry.value.object_data[a];
                    if (attribute.value.type != TG3_VALUE_INT) continue;
                    ref.bindings.push_back(
                        {tg3_string(attribute.key), static_cast<std::uint32_t>(attribute.value.int_val)});
                }
            }
        }
    }
    return ref;
}

std::vector<u8> base64_decode(const std::string& input)
{
    static const auto value_of = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::vector<u8> output;
    output.reserve(input.size() * 3 / 4);

    int accumulator = 0;
    int bits = 0;
    for (char c : input)
    {
        if (c == '=') break;
        const int value = value_of(c);
        if (value < 0) continue;
        accumulator = (accumulator << 6) | value;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            output.push_back(static_cast<u8>((accumulator >> bits) & 0xFF));
        }
    }
    return output;
}

std::vector<u8> image_bytes(const tg3_model& model, const tg3_image& image, const std::string& base_dir)
{
    if (image.buffer_view >= 0 && image.buffer_view < static_cast<int>(model.buffer_views_count))
    {
        const tg3_buffer_view& view = model.buffer_views[image.buffer_view];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers_count)) return {};
        const tg3_buffer& buffer = model.buffers[view.buffer];
        if (!buffer.data.data) return {};
        const u8* begin = buffer.data.data + view.byte_offset;
        return std::vector<u8>(begin, begin + view.byte_length);
    }

    const std::string uri = tg3_string(image.uri);
    if (uri.empty()) return {};

    if (uri.rfind("data:", 0) == 0)
    {
        const std::size_t marker = uri.find(";base64,");
        if (marker == std::string::npos) return {};
        return base64_decode(uri.substr(marker + 8));
    }

    std::string path = uri;
    if (!base_dir.empty()) path = fs::join(base_dir, uri);
    return fs::read_file(path);
}

std::vector<u8> load_image_rgba(const tg3_model& model, int image_index, const std::string& base_dir, int& width, int& height)
{
    width = 0;
    height = 0;
    if (image_index < 0 || image_index >= static_cast<int>(model.images_count)) return {};

    const std::vector<u8> encoded = image_bytes(model, model.images[image_index], base_dir);
    if (encoded.empty()) return {};

    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) return {};

    std::vector<u8> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    std::memcpy(rgba.data(), pixels, rgba.size());
    stbi_image_free(pixels);
    return rgba;
}

Vec3 vec3_from(const std::vector<float>& values, std::size_t base)
{
    if (base + 2 >= values.size()) return Vec3(0.0f);
    return Vec3(values[base + 0], values[base + 1], values[base + 2]);
}

Quat quat_from_xyzw(float x, float y, float z, float w)
{
    Quat result(w, x, y, z);
    const float length = glm::length(result);
    if (length > 0.000001f) result /= length;
    return result;
}

Quat quat_from_values(const std::vector<float>& values, std::size_t base)
{
    if (base + 3 >= values.size()) return Quat(1.0f, 0.0f, 0.0f, 0.0f);
    return quat_from_xyzw(values[base + 0], values[base + 1], values[base + 2], values[base + 3]);
}

// Hermite interpolation matching the glTF CUBICSPLINE definition.
float cubic_spline(float p0, float m0, float p1, float m1, float t, float delta)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    return h00 * p0 + h10 * delta * m0 + h01 * p1 + h11 * delta * m1;
}

// Transforms an axis-aligned box by a matrix, returning the box that contains
// all eight transformed corners.
Bounds transformed_bounds(const Bounds& bounds, const Mat4& transform)
{
    Bounds result;
    if (!bounds.valid()) return result;

    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner{
            (i & 1) ? bounds.max.x : bounds.min.x,
            (i & 2) ? bounds.max.y : bounds.min.y,
            (i & 4) ? bounds.max.z : bounds.min.z};
        result.expand(Vec3(transform * Vec4(corner, 1.0f)));
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// ModelData::evaluate
// ---------------------------------------------------------------------------

void detail::ModelData::evaluate() const
{
    const bool animate = playing && current_animation >= 0 &&
                         current_animation < static_cast<int>(animations.size());

    // While an animation is playing the pose must be re-evaluated every frame so
    // the current animation sample is applied to the nodes.  Otherwise we rely
    // on needs_evaluation to avoid unnecessary work.
    if (!needs_evaluation && !animate && node_globals.size() == nodes.size()) return;

    node_globals.assign(nodes.size(), Mat4(1.0f));
    const Animation* animation = animate ? &animations[static_cast<std::size_t>(current_animation)] : nullptr;

    auto compute_node = [&](auto&& self, int index) -> Mat4 {
        if (index < 0 || index >= static_cast<int>(nodes.size())) return Mat4(1.0f);
        if (node_globals[static_cast<std::size_t>(index)] != Mat4(1.0f)) return node_globals[static_cast<std::size_t>(index)];

        const Node& node = nodes[static_cast<std::size_t>(index)];

        Vec3 translation = node.translation;
        Quat rotation = node.rotation;
        Vec3 scale = node.scale;
        bool has_channel = false;

        if (animation)
        {
            for (const AnimationChannel& channel : animation->channels)
            {
                if (channel.node != index) continue;
                if (channel.times.empty() || channel.values.empty()) continue;

                const float time = animation_time;
                std::size_t key = 0;
                if (channel.times.size() > 1)
                {
                    const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
                    key = upper == channel.times.begin() ? 0 : static_cast<std::size_t>(upper - channel.times.begin() - 1);
                    if (key + 1 >= channel.times.size()) key = channel.times.size() - 2;
                }

                const std::size_t next = std::min(key + 1, channel.times.size() - 1);
                const float t0 = channel.times[key];
                const float t1 = channel.times[next];
                const float delta = std::max(t1 - t0, 0.000001f);
                const float alpha = glm::clamp((time - t0) / delta, 0.0f, 1.0f);

                if (channel.path == AnimationChannel::Path::Translation ||
                    channel.path == AnimationChannel::Path::Scale)
                {
                    Vec3 value;
                    if (channel.interpolation == AnimationChannel::Interpolation::Step || key == next)
                    {
                        const std::size_t base = channel.interpolation == AnimationChannel::Interpolation::CubicSpline
                                                     ? (key * 3 + 1) * 3
                                                     : key * 3;
                        value = vec3_from(channel.values, base);
                    }
                    else if (channel.interpolation == AnimationChannel::Interpolation::CubicSpline)
                    {
                        const std::size_t a_base = (key * 3 + 1) * 3;
                        const std::size_t a_out = (key * 3 + 2) * 3;
                        const std::size_t b_base = (next * 3 + 1) * 3;
                        const std::size_t b_in = (next * 3 + 0) * 3;
                        float v[3];
                        for (int c = 0; c < 3; ++c)
                            v[c] = cubic_spline(channel.values[a_base + c], channel.values[a_out + c],
                                                channel.values[b_base + c], channel.values[b_in + c], alpha, delta);
                        value = Vec3(v[0], v[1], v[2]);
                    }
                    else
                    {
                        const Vec3 a = vec3_from(channel.values, key * 3);
                        const Vec3 b = vec3_from(channel.values, next * 3);
                        value = glm::mix(a, b, alpha);
                    }

                    if (channel.path == AnimationChannel::Path::Translation)
                        translation = value;
                    else
                        scale = value;
                }
                else if (channel.path == AnimationChannel::Path::Rotation)
                {
                    if (channel.interpolation == AnimationChannel::Interpolation::Step || key == next)
                        rotation = quat_from_values(channel.values, key * 4);
                    else if (channel.interpolation == AnimationChannel::Interpolation::CubicSpline)
                    {
                        const std::size_t a_base = (key * 3 + 1) * 4;
                        const std::size_t b_base = (next * 3 + 1) * 4;
                        const std::size_t a_in = (key * 3 + 0) * 4;
                        const std::size_t a_out = (key * 3 + 2) * 4;
                        const std::size_t b_in = (next * 3 + 0) * 4;
                        const std::size_t b_out = (next * 3 + 2) * 4;
                        float q[4];
                        for (int c = 0; c < 4; ++c)
                        {
                            const float p0 = channel.values[a_base + c];
                            const float m0 = channel.values[a_out + c];
                            const float p1 = channel.values[b_base + c];
                            const float m1 = channel.values[b_in + c];
                            q[c] = cubic_spline(p0, m0, p1, m1, alpha, delta);
                        }
                        rotation = quat_from_xyzw(q[0], q[1], q[2], q[3]);
                    }
                    else
                    {
                        const Quat a = quat_from_values(channel.values, key * 4);
                        const Quat b = quat_from_values(channel.values, next * 4);
                        rotation = glm::slerp(a, b, alpha);
                    }
                }

                has_channel = true;
            }
        }

        Mat4 local = node.static_local;
        if (has_channel)
        {
            local = glm::translate(Mat4(1.0f), translation);
            local *= glm::mat4_cast(rotation);
            local = glm::scale(local, scale);
        }

        if (node.parent >= 0)
            local = self(self, node.parent) * local;

        node_globals[static_cast<std::size_t>(index)] = local;
        return local;
    };

    // Compute the global transform of every node.  compute_node() resolves the
    // parent chain on demand, so calling it for each node also covers joints and
    // any node that is not reachable from the default scene.  (Previously only
    // the scene roots were evaluated, which left every joint at identity and
    // collapsed skinned meshes.)
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
        compute_node(compute_node, i);

    for (Instance& instance : instances)
    {
        if (instance.node >= 0 && instance.node < static_cast<int>(node_globals.size()))
            instance.world = node_globals[static_cast<std::size_t>(instance.node)];
        else
            instance.world = instance.static_world;

        instance.bones.clear();
        if (instance.skin >= 0 && instance.skin < static_cast<int>(skins.size()))
        {
            const Skin& skin = skins[static_cast<std::size_t>(instance.skin)];

            instance.bones.reserve(skin.joints.size());
            for (std::size_t j = 0; j < skin.joints.size(); ++j)
            {
                const int joint = skin.joints[j];
                Mat4 joint_global(1.0f);
                if (joint >= 0 && joint < static_cast<int>(node_globals.size()))
                    joint_global = node_globals[static_cast<std::size_t>(joint)];

                Mat4 inverse_bind(1.0f);
                if (j < skin.inverse_bind_matrices.size())
                    inverse_bind = skin.inverse_bind_matrices[j];

                // Skinning matrix per the glTF specification:
                //   vertex_world = joint_global * inverse_bind * vertex_local
                // The mesh node's transform is ignored for skinned meshes (see
                // draw_model), which is why this matrix does not include
                // instance.world.
                instance.bones.push_back(joint_global * inverse_bind);
            }
        }
    }

    needs_evaluation = false;
}

// ---------------------------------------------------------------------------
// Engine::Impl
// ---------------------------------------------------------------------------

struct Engine::Impl
{
    Config config;
    Camera* camera = nullptr;
    std::unique_ptr<platform::Window> window;
    platform::Renderer renderer;
    MeshPipeline pipeline;
    Sky sky;
    bool sky_enabled = false;

    Diligent::RefCntAutoPtr<Diligent::ITexture> default_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> default_texture_view;
    Diligent::RefCntAutoPtr<Diligent::ISampler> default_sampler;

    detail::Material default_material;

    std::vector<std::weak_ptr<detail::ModelData>> models;
    std::vector<std::string> search_paths;

    // Last captured back buffer, used by validate_last_frame().
    std::vector<u8> last_frame_rgba;
    u32 last_frame_width = 0;
    u32 last_frame_height = 0;

    Model cube;

    std::chrono::steady_clock::time_point last_time;
    double elapsed = 0.0;
    float delta_time = 0.0f;
    u64 frame_index = 0;

    bool initialized = false;
    bool running = false;
    bool skin_enabled = true;

    bool create_default_resources();
    void release_default_resources();

    bool upload_mesh(detail::GpuMesh& mesh, const std::vector<Vertex>& vertices, const std::vector<u32>& indices);
    void upload_texture(detail::Material& material, const std::vector<u8>& rgba, int width, int height,
                        const std::string& name);

    Model load_model_file(const std::string& path);
    Model build_cube(float size, const Color& color);
    Model build_mesh(const MeshData& mesh_data, const std::string& name = "Terrain");
    void draw_model(const detail::ModelData& model, const Mat4& transform, const Color* color_override);

    void register_model(const std::shared_ptr<detail::ModelData>& data)
    {
        models.push_back(data);
    }
};

bool Engine::Impl::create_default_resources()
{
    auto* device = renderer.device();
    if (!device) return false;

    const u8 white[4] = {255, 255, 255, 255};
    Diligent::TextureDesc desc;
    desc.Name = "Insland white texture";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.Usage = Diligent::USAGE_IMMUTABLE;

    Diligent::TextureSubResData subresource(white, 4);
    Diligent::TextureData data(&subresource, 1);
    device->CreateTexture(desc, &data, &default_texture);
    if (!default_texture) return false;
    default_texture_view = default_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    if (!default_texture_view) return false;

    Diligent::SamplerDesc sampler_desc;
    sampler_desc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler_desc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler_desc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler_desc.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
    sampler_desc.MaxAnisotropy = 1;
    device->CreateSampler(sampler_desc, &default_sampler);
    if (!default_sampler) return false;

    default_material.name = "Default";
    default_material.base_color = Color::white();
    default_material.texture_view = default_texture_view;
    default_material.sampler = default_sampler;
    return true;
}

void Engine::Impl::release_default_resources()
{
    default_sampler.Release();
    default_texture_view.Release();
    default_texture.Release();
}

bool Engine::Impl::upload_mesh(detail::GpuMesh& mesh, const std::vector<Vertex>& vertices, const std::vector<u32>& indices)
{
    auto* device = renderer.device();
    if (!device || vertices.empty() || indices.empty()) return false;

    mesh.indices = indices;
    mesh.index_count = static_cast<u32>(indices.size());

    Diligent::BufferDesc vertex_desc;
    vertex_desc.Name = (mesh.name + " vertices").c_str();
    vertex_desc.Size = vertices.size() * sizeof(Vertex);
    vertex_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vertex_desc.Usage = Diligent::USAGE_IMMUTABLE;
    Diligent::BufferData vertex_data(vertices.data(), vertex_desc.Size);
    device->CreateBuffer(vertex_desc, &vertex_data, &mesh.vertex_buffer);
    if (!mesh.vertex_buffer) return false;

    Diligent::BufferDesc index_desc;
    index_desc.Name = (mesh.name + " indices").c_str();
    index_desc.Size = indices.size() * sizeof(u32);
    index_desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    index_desc.Usage = Diligent::USAGE_IMMUTABLE;
    Diligent::BufferData index_data(indices.data(), index_desc.Size);
    device->CreateBuffer(index_desc, &index_data, &mesh.index_buffer);
    return mesh.index_buffer != nullptr;
}

void Engine::Impl::upload_texture(detail::Material& material, const std::vector<u8>& rgba, int width, int height,
                                  const std::string& name)
{
    auto* device = renderer.device();
    if (!device || rgba.empty() || width <= 0 || height <= 0) return;

    Diligent::TextureDesc desc;
    desc.Name = name.c_str();
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<Diligent::Uint32>(width);
    desc.Height = static_cast<Diligent::Uint32>(height);
    desc.MipLevels = 1;
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.Usage = Diligent::USAGE_IMMUTABLE;

    Diligent::TextureSubResData subresource(rgba.data(), static_cast<Diligent::Uint64>(width) * 4u);
    Diligent::TextureData data(&subresource, 1);
    device->CreateTexture(desc, &data, &material.texture);
    if (material.texture)
        material.texture_view = material.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    material.sampler = default_sampler;
}

Model Engine::Impl::build_cube(float size, const Color& color)
{
    const float h = size * 0.5f;
    const Vec3 positions[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const Vec3 ups[6] = {
        {0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};

    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    vertices.reserve(24);
    indices.reserve(36);

    for (int face = 0; face < 6; ++face)
    {
        const Vec3 normal = positions[face];
        Vec3 right = glm::cross(ups[face], normal);
        if (glm::length(right) < 0.0001f) right = Vec3(1, 0, 0);
        right = glm::normalize(right);
        const Vec3 up = glm::normalize(glm::cross(normal, right));

        const Vec3 center = normal * h;
        const Vec3 corners[4] = {
            center + (-right - up) * h,
            center + (right - up) * h,
            center + (right + up) * h,
            center + (-right + up) * h};

        const Vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const u32 base = static_cast<u32>(vertices.size());

        for (int i = 0; i < 4; ++i)
        {
            Vertex vertex;
            vertex.position = corners[i];
            vertex.normal = normal;
            vertex.uv = uvs[i];
            vertex.tangent = Vec4(right, 1.0f);
            vertices.push_back(vertex);
        }

        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    auto data = std::make_shared<detail::ModelData>();
    data->name = "Cube";

    detail::Material material;
    material.name = "Cube material";
    material.base_color = color;
    material.metallic = 0.0f;
    material.roughness = 0.65f;
    material.texture_view = default_texture_view;
    material.sampler = default_sampler;
    data->materials.push_back(material);

    detail::GpuMesh mesh;
    mesh.name = "Cube";
    mesh.material_index = 0;
    for (const Vertex& vertex : vertices) mesh.bounds.expand(vertex.position);
    if (!upload_mesh(mesh, vertices, indices))
    {
        log_message("failed to create cube GPU buffers");
        return Model{};
    }
    data->meshes.push_back(std::move(mesh));
    data->bounds = data->meshes.front().bounds;

    detail::Instance instance;
    instance.mesh = 0;
    instance.node = -1;
    instance.static_world = Mat4(1.0f);
    instance.world = Mat4(1.0f);
    data->instances.push_back(instance);

    data->needs_evaluation = true;
    register_model(data);
    return Model(data);
}

// Builds a mesh from procedural data (used by the world / UI systems).
Model Engine::Impl::build_mesh(const MeshData& source, const std::string& name)
{
    if (!initialized || !pipeline.valid() || source.positions.empty() || source.indices.empty())
        return Model{};

    const std::size_t vertex_count = source.positions.size();
    std::vector<Vertex> vertices(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i)
    {
        Vertex& vertex = vertices[i];
        vertex.position = source.positions[i];
        if (i < source.normals.size()) vertex.normal = source.normals[i];
        if (i < source.colors.size()) vertex.color = source.colors[i];
    }

    auto data = std::make_shared<detail::ModelData>();
    data->name = name;

    detail::Material material;
    material.name = name + " material";
    material.base_color = source.tint;
    material.metallic = 0.0f;
    material.roughness = 0.85f;
    material.texture_view = default_texture_view;
    material.sampler = default_sampler;
    data->materials.push_back(material);

    detail::GpuMesh mesh;
    mesh.name = name;
    mesh.material_index = 0;
    for (const Vertex& vertex : vertices) mesh.bounds.expand(vertex.position);
    if (!upload_mesh(mesh, vertices, source.indices))
    {
        log_message("failed to create mesh GPU buffers");
        return Model{};
    }
    data->meshes.push_back(std::move(mesh));
    data->bounds = data->meshes.front().bounds;

    detail::Instance instance;
    instance.mesh = 0;
    instance.node = -1;
    instance.static_world = Mat4(1.0f);
    instance.world = Mat4(1.0f);
    data->instances.push_back(instance);

    data->needs_evaluation = true;
    register_model(data);
    return Model(data);
}

void Engine::Impl::draw_model(const detail::ModelData& model, const Mat4& transform, const Color* color_override)
{
    if (!pipeline.valid() || model.meshes.empty()) return;

    const_cast<detail::ModelData&>(model).evaluate();

    const float aspect = renderer.aspect_ratio();
    const Mat4 view_projection = camera ? camera->view_projection(aspect) : Mat4(1.0f);
    const Vec3 camera_position = camera ? camera->position : Vec3(0.0f);
    const Vec3 light_direction = glm::normalize(Vec3(-0.45f, -1.0f, -0.35f));

    for (const detail::Instance& instance : model.instances)
    {
        if (instance.mesh >= model.meshes.size()) continue;
        const detail::GpuMesh& mesh = model.meshes[instance.mesh];
        if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count == 0) continue;

        const detail::Material* material = &default_material;
        if (mesh.material_index >= 0 && mesh.material_index < static_cast<int>(model.materials.size()))
            material = &model.materials[static_cast<std::size_t>(mesh.material_index)];

        Uniforms uniforms;
        uniforms.view_proj = view_projection;
        // Per the glTF spec, a skinned mesh ignores the transform of the node it
        // is attached to; only the joint matrices (which already contain the
        // skeleton's scene transform) are applied.  The engine's external model
        // transform is still applied so games can position/scale the model.
        const bool skinned = !instance.bones.empty() && skin_enabled;
        uniforms.model = skinned ? transform : transform * instance.world;
        uniforms.normal_matrix = Mat4(glm::mat3(glm::transpose(glm::inverse(glm::mat3(uniforms.model)))));
        uniforms.base_color = color_override ? color_override->to_vec4() : material->base_color.to_vec4();
        uniforms.params = Vec4{
            material->texture_view ? 1.0f : 0.0f,
            skinned ? 1.0f : 0.0f,
            material->metallic,
            material->roughness};
        uniforms.camera_position = Vec4(camera_position, 1.0f);
        uniforms.light_direction = Vec4(light_direction, 0.0f);
        uniforms.light_color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);

        Diligent::ITextureView* texture = material->texture_view ? material->texture_view : default_texture_view;
        Diligent::ISampler* sampler = material->sampler ? material->sampler : default_sampler;

        const Mat4* bones = skinned ? instance.bones.data() : nullptr;
        const std::uint32_t bone_count = skinned ? static_cast<std::uint32_t>(instance.bones.size()) : 0;

        pipeline.draw(renderer, uniforms, bones, bone_count, texture, sampler, mesh.vertex_buffer,
                      mesh.index_buffer, mesh.index_count);
    }
}

Model Engine::Impl::load_model_file(const std::string& path)
{
    std::string resolved = path;
    if (!fs::exists(resolved))
    {
        const auto found = fs::find(path, search_paths);
        if (found)
            resolved = *found;
        else if (const std::string located = fs::locate_asset(path); !located.empty())
            resolved = located;
        else
        {
            log_message("model not found: " + path);
            return Model{};
        }
    }

    std::vector<u8> file = fs::read_file(resolved);
    if (file.empty())
    {
        log_message("failed to read model: " + resolved);
        return Model{};
    }

    const std::string base_dir = fs::parent(resolved);

    tg3_parse_options options;
    tg3_parse_options_init(&options);
    options.images_as_is = 1; // The engine decodes images itself with stb_image.
    options.fs.file_exists = [](const char* file_path, uint32_t file_path_len, void*) -> int32_t {
        return fs::exists(std::string(file_path, file_path_len)) ? 1 : 0;
    };
    options.fs.read_file = [](uint8_t** out_data, uint64_t* out_size, const char* file_path, uint32_t file_path_len,
                              void*) -> int32_t {
        const std::vector<u8> bytes = fs::read_file(std::string(file_path, file_path_len));
        if (bytes.empty()) return 0;
        auto* memory = static_cast<uint8_t*>(std::malloc(bytes.size()));
        if (!memory) return 0;
        std::memcpy(memory, bytes.data(), bytes.size());
        *out_data = memory;
        *out_size = bytes.size();
        return 1;
    };
    options.fs.free_file = [](uint8_t* data, uint64_t, void*) { std::free(data); };
    options.fs.get_file_size = [](uint64_t* out_size, const char* file_path, uint32_t file_path_len, void*) -> int32_t {
        *out_size = fs::file_size(std::string(file_path, file_path_len));
        return *out_size > 0 ? 1 : 0;
    };

    tg3_error_stack errors;
    tg3_error_stack_init(&errors);

    tg3_model model{};
    const tg3_error_code error = tg3_parse_auto(&model, &errors, file.data(), file.size(), base_dir.data(),
                                                 static_cast<uint32_t>(base_dir.size()), &options);
    if (error != TG3_OK)
    {
        log_message("glTF parse failed for " + resolved + " (" + std::to_string(tg3_errors_count(&errors)) + " errors)");
        tg3_model_free(&model);
        tg3_error_stack_free(&errors);
        return Model{};
    }

    auto data = std::make_shared<detail::ModelData>();
    data->name = fs::stem(resolved);

    // Materials and textures ------------------------------------------------
    data->materials.reserve(model.materials_count + 1);
    for (uint32_t i = 0; i < model.materials_count; ++i)
    {
        const tg3_material& source = model.materials[i];
        detail::Material material;
        material.name = tg3_string(source.name);
        material.base_color = Color{
            static_cast<float>(source.pbr_metallic_roughness.base_color_factor[0]),
            static_cast<float>(source.pbr_metallic_roughness.base_color_factor[1]),
            static_cast<float>(source.pbr_metallic_roughness.base_color_factor[2]),
            static_cast<float>(source.pbr_metallic_roughness.base_color_factor[3])};
        material.metallic = static_cast<float>(source.pbr_metallic_roughness.metallic_factor);
        material.roughness = static_cast<float>(source.pbr_metallic_roughness.roughness_factor);
        material.double_sided = source.double_sided != 0;
        const std::string alpha_mode = tg3_string(source.alpha_mode);
        material.alpha_blend = alpha_mode == "BLEND";
        material.sampler = default_sampler;

        const int texture_index = source.pbr_metallic_roughness.base_color_texture.index;
        if (texture_index >= 0 && texture_index < static_cast<int>(model.textures_count))
        {
            const tg3_texture& texture = model.textures[texture_index];
            if (texture.source >= 0)
            {
                int width = 0;
                int height = 0;
                std::vector<u8> pixels = load_image_rgba(model, texture.source, base_dir, width, height);
                if (!pixels.empty())
                    upload_texture(material, pixels, width, height, material.name + " base color");
                else
                    log_message("could not decode texture for material " + material.name);
            }
        }

        data->materials.push_back(std::move(material));
    }

    const int default_material_index = static_cast<int>(data->materials.size());
    {
        detail::Material fallback;
        fallback.name = "Default";
        fallback.base_color = Color::white();
        fallback.metallic = 0.0f;
        fallback.roughness = 0.8f;
        fallback.texture_view = default_texture_view;
        fallback.sampler = default_sampler;
        data->materials.push_back(std::move(fallback));
    }

    // Meshes ----------------------------------------------------------------
    std::vector<std::vector<int>> mesh_primitives(model.meshes_count);

    // Per-vertex skinning data kept on the CPU until the bind-pose bounds are
    // computed.  Skinned bounds cannot be derived from the mesh AABB alone:
    // transforming the whole AABB by every bone matrix is far too conservative
    // (a bone whose inverse bind matrix contains a large scale would inflate
    // the bounds by that scale).  We therefore evaluate the actual skinned
    // positions once at load time and release this data afterwards.
    struct SkinVertex
    {
        Vec3 position;
        Vec4 joints;
        Vec4 weights;
    };
    std::vector<std::vector<SkinVertex>> mesh_skin_vertices;

    for (uint32_t mesh_index = 0; mesh_index < model.meshes_count; ++mesh_index)
    {
        const tg3_mesh& source_mesh = model.meshes[mesh_index];
        for (uint32_t primitive_index = 0; primitive_index < source_mesh.primitives_count; ++primitive_index)
        {
            const tg3_primitive& primitive = source_mesh.primitives[primitive_index];
            if (primitive.mode >= 0 && primitive.mode != 4) continue; // Only TRIANGLES for now.

            int position_accessor = -1;
            int normal_accessor = -1;
            int uv_accessor = -1;
            int tangent_accessor = -1;
            int joints_accessor = -1;
            int weights_accessor = -1;

            for (uint32_t a = 0; a < primitive.attributes_count; ++a)
            {
                const tg3_str_int_pair& attribute = primitive.attributes[a];
                if (attribute_is(attribute.key, "POSITION")) position_accessor = attribute.value;
                else if (attribute_is(attribute.key, "NORMAL")) normal_accessor = attribute.value;
                else if (attribute_is(attribute.key, "TEXCOORD_0")) uv_accessor = attribute.value;
                else if (attribute_is(attribute.key, "TANGENT")) tangent_accessor = attribute.value;
                else if (attribute_is(attribute.key, "JOINTS_0")) joints_accessor = attribute.value;
                else if (attribute_is(attribute.key, "WEIGHTS_0")) weights_accessor = attribute.value;
            }

            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<float> uvs;
            std::vector<float> tangents;
            std::vector<float> joints;
            std::vector<float> weights;
            std::vector<u32> indices;
            std::size_t vertex_count = 0;

            const DracoRef draco = find_draco_extension(primitive);
            draco_loader::DecodedPrimitive decoded;
            bool draco_decoded = false;

            if (draco.found && draco.buffer_view >= 0 &&
                draco.buffer_view < static_cast<int>(model.buffer_views_count))
            {
                const tg3_buffer_view& view = model.buffer_views[draco.buffer_view];
                if (view.buffer >= 0 && view.buffer < static_cast<int>(model.buffers_count))
                {
                    const tg3_buffer& buffer = model.buffers[view.buffer];
                    if (buffer.data.data)
                    {
                        draco_decoded = draco_loader::decode(
                            buffer.data.data + view.byte_offset,
                            static_cast<std::size_t>(view.byte_length), draco.bindings, decoded);
                    }
                }
                if (!draco_decoded)
                    log_message("failed to decode Draco primitive, falling back to accessors");
            }

            if (draco_decoded)
            {
                positions = std::move(decoded.positions);
                normals = std::move(decoded.normals);
                uvs = std::move(decoded.uvs);
                tangents = std::move(decoded.tangents);
                joints = std::move(decoded.joints);
                weights = std::move(decoded.weights);
                indices = std::move(decoded.indices);
                vertex_count = decoded.vertex_count;
            }
            else
            {
                if (position_accessor < 0) continue;

                int components = 0;
                if (!read_accessor(model, position_accessor, positions, components) || components < 3) continue;

                vertex_count = positions.size() / static_cast<std::size_t>(components);
                if (vertex_count == 0) continue;

                if (normal_accessor >= 0) read_accessor(model, normal_accessor, normals, components);
                if (uv_accessor >= 0) read_accessor(model, uv_accessor, uvs, components);
                if (tangent_accessor >= 0) read_accessor(model, tangent_accessor, tangents, components);
                if (joints_accessor >= 0) read_accessor(model, joints_accessor, joints, components);
                if (weights_accessor >= 0) read_accessor(model, weights_accessor, weights, components);

                indices = read_indices(model, primitive.indices, vertex_count);
            }

            if (indices.size() < 3) continue;

            std::vector<Vertex> vertices(vertex_count);
            for (std::size_t i = 0; i < vertex_count; ++i)
            {
                Vertex& vertex = vertices[i];
                vertex.position = Vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
                if (normals.size() >= (i + 1) * 3)
                    vertex.normal = glm::normalize(Vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]));
                if (uvs.size() >= (i + 1) * 2)
                    vertex.uv = Vec2(uvs[i * 2 + 0], uvs[i * 2 + 1]);
                if (tangents.size() >= (i + 1) * 4)
                    vertex.tangent = Vec4(tangents[i * 4 + 0], tangents[i * 4 + 1], tangents[i * 4 + 2], tangents[i * 4 + 3]);
                if (joints.size() >= (i + 1) * 4)
                    vertex.joints = Vec4(joints[i * 4 + 0], joints[i * 4 + 1], joints[i * 4 + 2], joints[i * 4 + 3]);
                if (weights.size() >= (i + 1) * 4)
                {
                    Vec4 w(weights[i * 4 + 0], weights[i * 4 + 1], weights[i * 4 + 2], weights[i * 4 + 3]);
                    const float sum = w.x + w.y + w.z + w.w;
                    if (sum > 0.000001f) w /= sum;
                    vertex.weights = w;
                }
            }

            if (indices.size() < 3) continue;

            detail::GpuMesh mesh;
            mesh.name = tg3_string(source_mesh.name);
            if (mesh.name.empty()) mesh.name = "Mesh " + std::to_string(mesh_index) + "." + std::to_string(primitive_index);
            mesh.material_index = primitive.material >= 0 ? primitive.material : default_material_index;

            for (const Vertex& vertex : vertices) mesh.bounds.expand(vertex.position);

            std::vector<SkinVertex> skin_vertices;
            if (!joints.empty() && !weights.empty())
            {
                skin_vertices.reserve(vertices.size());
                for (const Vertex& vertex : vertices)
                    skin_vertices.push_back(SkinVertex{vertex.position, vertex.joints, vertex.weights});
            }

            if (!upload_mesh(mesh, vertices, indices))
            {
                log_message("failed to upload mesh " + mesh.name);
                continue;
            }

            data->bounds.expand(mesh.bounds);
            mesh_primitives[mesh_index].push_back(static_cast<int>(data->meshes.size()));
            data->meshes.push_back(std::move(mesh));
            mesh_skin_vertices.push_back(std::move(skin_vertices));
        }
    }

    // Nodes -----------------------------------------------------------------
    data->nodes.resize(model.nodes_count);
    for (uint32_t i = 0; i < model.nodes_count; ++i)
    {
        const tg3_node& source = model.nodes[i];
        detail::Node& node = data->nodes[i];
        node.name = tg3_string(source.name);
        node.mesh = source.mesh;
        node.skin = source.skin;
        node.translation = Vec3(static_cast<float>(source.translation[0]), static_cast<float>(source.translation[1]),
                                static_cast<float>(source.translation[2]));
        node.rotation = quat_from_xyzw(static_cast<float>(source.rotation[0]), static_cast<float>(source.rotation[1]),
                                       static_cast<float>(source.rotation[2]), static_cast<float>(source.rotation[3]));
        node.scale = Vec3(static_cast<float>(source.scale[0]), static_cast<float>(source.scale[1]),
                          static_cast<float>(source.scale[2]));

        if (source.has_matrix)
            node.static_local = matrix_from_doubles(source.matrix);
        else
        {
            node.static_local = glm::translate(Mat4(1.0f), node.translation);
            node.static_local *= glm::mat4_cast(node.rotation);
            node.static_local = glm::scale(node.static_local, node.scale);
        }

        for (uint32_t c = 0; c < source.children_count; ++c)
        {
            const int child = source.children[c];
            node.children.push_back(child);
            if (child >= 0 && child < static_cast<int>(data->nodes.size()))
                data->nodes[static_cast<std::size_t>(child)].parent = static_cast<int>(i);
        }
    }

    // Skins -----------------------------------------------------------------
    data->skins.resize(model.skins_count);
    for (uint32_t i = 0; i < model.skins_count; ++i)
    {
        const tg3_skin& source = model.skins[i];
        detail::Skin& skin = data->skins[i];
        skin.joints.reserve(source.joints_count);
        for (uint32_t j = 0; j < source.joints_count; ++j) skin.joints.push_back(source.joints[j]);

        std::vector<float> values;
        int components = 0;
        if (source.inverse_bind_matrices >= 0 &&
            read_accessor(model, source.inverse_bind_matrices, values, components) && components == 16)
        {
            const std::size_t count = values.size() / 16;
            skin.inverse_bind_matrices.resize(count);
            for (std::size_t m = 0; m < count; ++m)
            {
                Mat4 matrix(1.0f);
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        matrix[column][row] = values[m * 16 + static_cast<std::size_t>(column * 4 + row)];
                skin.inverse_bind_matrices[m] = matrix;
            }
        }

        if (skin.joints.size() > kMaxBones)
        {
            log_message("skin " + std::to_string(i) + " has " + std::to_string(skin.joints.size()) +
                        " joints; only the first " + std::to_string(kMaxBones) + " will be uploaded to the GPU");
        }
    }

    // Animation -------------------------------------------------------------
    data->animations.resize(model.animations_count);
    for (uint32_t i = 0; i < model.animations_count; ++i)
    {
        const tg3_animation& source = model.animations[i];
        detail::Animation& animation = data->animations[i];
        animation.name = tg3_string(source.name);
        if (animation.name.empty()) animation.name = "Animation " + std::to_string(i);

        for (uint32_t c = 0; c < source.channels_count; ++c)
        {
            const tg3_animation_channel& source_channel = source.channels[c];
            if (source_channel.sampler < 0 || source_channel.sampler >= static_cast<int>(source.samplers_count))
                continue;
            if (source_channel.target.node < 0) continue;

            const tg3_animation_sampler& source_sampler = source.samplers[source_channel.sampler];
            const std::string path = tg3_string(source_channel.target.path);

            detail::AnimationChannel channel;
            channel.node = source_channel.target.node;
            if (path == "rotation") channel.path = detail::AnimationChannel::Path::Rotation;
            else if (path == "scale") channel.path = detail::AnimationChannel::Path::Scale;
            else if (path == "translation") channel.path = detail::AnimationChannel::Path::Translation;
            else continue;

            const std::string interpolation = tg3_string(source_sampler.interpolation);
            if (interpolation == "STEP") channel.interpolation = detail::AnimationChannel::Interpolation::Step;
            else if (interpolation == "CUBICSPLINE") channel.interpolation = detail::AnimationChannel::Interpolation::CubicSpline;
            else channel.interpolation = detail::AnimationChannel::Interpolation::Linear;

            int components = 0;
            if (!read_accessor(model, source_sampler.input, channel.times, components) || components != 1) continue;
            if (!read_accessor(model, source_sampler.output, channel.values, components)) continue;
            if (channel.times.empty() || channel.values.empty()) continue;

            animation.duration = std::max(animation.duration, channel.times.back());
            animation.channels.push_back(std::move(channel));
        }
    }

    // Scene roots and instances --------------------------------------------
    if (model.scenes_count > 0 && model.default_scene >= 0 &&
        model.default_scene < static_cast<int>(model.scenes_count))
    {
        const tg3_scene& scene = model.scenes[model.default_scene];
        for (uint32_t i = 0; i < scene.nodes_count; ++i) data->roots.push_back(scene.nodes[i]);
    }

    if (data->roots.empty())
    {
        for (int i = 0; i < static_cast<int>(data->nodes.size()); ++i)
            if (data->nodes[static_cast<std::size_t>(i)].parent < 0) data->roots.push_back(i);
    }

    for (int i = 0; i < static_cast<int>(data->nodes.size()); ++i)
    {
        const detail::Node& node = data->nodes[static_cast<std::size_t>(i)];
        if (node.mesh < 0 || node.mesh >= static_cast<int>(mesh_primitives.size())) continue;

        for (int mesh : mesh_primitives[static_cast<std::size_t>(node.mesh)])
        {
            detail::Instance instance;
            instance.mesh = static_cast<u32>(mesh);
            instance.node = i;
            instance.skin = node.skin;
            instance.static_world = node.static_local;
            instance.world = node.static_local;
            data->instances.push_back(std::move(instance));
        }
    }

    data->needs_evaluation = true;
    data->evaluate();

    // Recompute the bounds in scene space at the bind pose.  The raw mesh
    // bounds only describe the vertex data in mesh-local space; skinned meshes
    // additionally depend on the joint transforms (and ignore the mesh node's
    // own transform).  This keeps Model::bounds() meaningful for auto-fitting
    // models in game code.
    Bounds scene_bounds;
    for (const detail::Instance& instance : data->instances)
    {
        if (instance.mesh >= data->meshes.size()) continue;
        const detail::GpuMesh& mesh = data->meshes[instance.mesh];
        if (!mesh.bounds.valid()) continue;

        const std::vector<SkinVertex>* skin_vertices =
            instance.mesh < mesh_skin_vertices.size() ? &mesh_skin_vertices[instance.mesh] : nullptr;

        if (!instance.bones.empty() && skin_vertices && !skin_vertices->empty())
        {
            // Evaluate the real skinned positions at the bind pose.  Using the
            // per-bone AABB transform here would be far too conservative for
            // files whose inverse bind matrices contain large scales (e.g. some
            // Sketchfab exports), which makes auto-fitting shrink the model to
            // nothing.
            for (const SkinVertex& vertex : *skin_vertices)
            {
                Mat4 blend(0.0f);
                for (int k = 0; k < 4; ++k)
                {
                    const float weight = vertex.weights[k];
                    if (weight == 0.0f) continue;

                    const int joint = static_cast<int>(vertex.joints[k]);
                    if (joint < 0 || joint >= static_cast<int>(instance.bones.size())) continue;
                    blend += weight * instance.bones[static_cast<std::size_t>(joint)];
                }
                scene_bounds.expand(Vec3(blend * Vec4(vertex.position, 1.0f)));
            }
        }
        else
        {
            scene_bounds.expand(transformed_bounds(mesh.bounds, instance.world));
        }
    }
    if (scene_bounds.valid()) data->bounds = scene_bounds;

    // Keep the CPU-side buffers alive while the GPU loads are complete.
    tg3_model_free(&model);
    tg3_error_stack_free(&errors);

    register_model(data);
    return Model(data);
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

Engine engine;

Engine::Engine() = default;

Engine::~Engine()
{
    shutdown();
}

bool Engine::init()
{
    return init(Config{});
}

bool Engine::init(const Config& config)
{
    shutdown();

    impl_ = std::make_unique<Impl>();
    impl_->config = config;
    impl_->camera = &camera_;

    platform::WindowDesc window_desc;
    window_desc.title = config.title;
    window_desc.width = config.width;
    window_desc.height = config.height;
    window_desc.vsync = config.vsync;
    window_desc.fullscreen = config.fullscreen;
    window_desc.resizable = config.resizable;

    if (config.native_window)
        impl_->window = platform::Window::from_native_handle(config.native_window, config.width, config.height);
    else
        impl_->window = platform::Window::create(window_desc);

    if (!impl_->window)
    {
        log_message("failed to create window");
        impl_.reset();
        return false;
    }

    platform::RendererDesc renderer_desc;
    renderer_desc.vsync = config.vsync;
    renderer_desc.validation = config.validation;

    if (!impl_->renderer.init(*impl_->window, renderer_desc))
    {
        log_message("failed to initialize renderer");
        impl_.reset();
        return false;
    }

    impl_->renderer.set_clear_color(config.clear_color);

    if (!impl_->pipeline.create(impl_->renderer))
    {
        log_message("failed to create mesh pipeline");
        impl_.reset();
        return false;
    }

    // The sky pass is optional: if the shader fails on a backend the engine
    // still renders geometry with the clear colour as background.
    if (impl_->sky.create(impl_->renderer))
        impl_->sky_enabled = true;
    else
        log_message("sky pipeline unavailable, using clear colour");

    if (!impl_->create_default_resources())
    {
        log_message("failed to create default GPU resources");
        impl_.reset();
        return false;
    }

    camera_.zero_to_one_ndc = impl_->renderer.uses_zero_to_one_ndc();
    camera_.position = Vec3(0.0f, 1.8f, 4.5f);
    camera_.target = Vec3(0.0f, 0.5f, 0.0f);

    // Asset search paths ----------------------------------------------------
    auto add_path = [&](const std::string& path) {
        if (path.empty()) return;
        if (std::find(impl_->search_paths.begin(), impl_->search_paths.end(), path) != impl_->search_paths.end()) return;
        impl_->search_paths.push_back(path);
    };

    add_path(config.asset_root);
    add_path(fs::current_directory());
    add_path(fs::join(fs::current_directory(), "assets"));
    const std::string executable_dir = fs::executable_directory();
    add_path(executable_dir);
    add_path(fs::join(executable_dir, "assets"));

    // Standard package/user layout from the filesystem module (also seeds the
    // per-user directories for logs, saves and configs).
    fs::init_standard_paths(config.asset_root);

    impl_->cube = impl_->build_cube(1.0f, Color{0.82f, 0.42f, 0.24f, 1.0f});
    if (!impl_->cube.valid())
    {
        log_message("failed to create default cube");
        impl_.reset();
        return false;
    }

    impl_->last_time = std::chrono::steady_clock::now();
    impl_->elapsed = 0.0;
    impl_->delta_time = 0.0f;
    impl_->frame_index = 0;
    impl_->initialized = true;
    impl_->running = true;
    return true;
}

void Engine::shutdown()
{
    if (!impl_) return;

    impl_->models.clear();
    impl_->cube = Model{};
    impl_->sky.shutdown();
    impl_->pipeline.shutdown();
    impl_->release_default_resources();
    impl_->renderer.shutdown();
    impl_->window.reset();
    impl_->initialized = false;
    impl_->running = false;
    impl_.reset();
}

bool Engine::initialized() const
{
    return impl_ && impl_->initialized;
}

bool Engine::running() const
{
    return impl_ && impl_->running && impl_->window && !impl_->window->should_close();
}

void Engine::poll_events()
{
    if (!impl_ || !impl_->window) return;
    if (!impl_->window->poll_events()) impl_->running = false;
}

float Engine::begin_frame()
{
    if (!impl_ || !impl_->initialized) return 0.0f;

    poll_events();
    if (!running()) return 0.0f;

    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = now - impl_->last_time;
    impl_->last_time = now;

    impl_->delta_time = std::min(delta.count(), 0.1f);
    impl_->elapsed += impl_->delta_time;

    update(impl_->delta_time);
    impl_->renderer.begin_frame();
    if (impl_->sky_enabled && impl_->sky.valid())
        impl_->sky.draw(impl_->renderer, camera_, impl_->elapsed);
    ++impl_->frame_index;
    return impl_->delta_time;
}

void Engine::end_frame()
{
    if (!impl_ || !impl_->initialized) return;
    impl_->renderer.end_frame();
    platform::end_input_frame();
}

void Engine::update(float delta_time)
{
    if (!impl_) return;

    for (auto it = impl_->models.begin(); it != impl_->models.end();)
    {
        if (auto model = it->lock())
        {
            model->update(delta_time);
            ++it;
        }
        else
        {
            it = impl_->models.erase(it);
        }
    }
}

Model Engine::load_model(ModelFormat, const std::string& path)
{
    if (!impl_ || !impl_->initialized) return Model{};
    return impl_->load_model_file(path);
}

Model Engine::load_model(const std::string& path)
{
    return load_model(GLTF, path);
}

Model Engine::create_cube(float size, const Color& color)
{
    if (!impl_ || !impl_->initialized) return Model{};
    return impl_->build_cube(size, color);
}

Model Engine::create_model(const MeshData& geometry)
{
    if (!impl_ || !impl_->initialized) return Model{};
    return impl_->build_mesh(geometry);
}

void Engine::draw(const Model& model, const Transform& transform)
{
    if (!model.valid()) return;
    impl_->draw_model(*model.data(), transform.matrix(), nullptr);
}

void Engine::draw(const Model& model, const Mat4& transform)
{
    if (!model.valid()) return;
    impl_->draw_model(*model.data(), transform, nullptr);
}

void Engine::draw_cube(const Transform& transform, const Color& color)
{
    if (!impl_ || !impl_->cube.valid()) return;
    impl_->draw_model(*impl_->cube.data(), transform.matrix(), &color);
}

void Engine::set_clear_color(const Color& color)
{
    if (!impl_) return;
    impl_->config.clear_color = color;
    impl_->renderer.set_clear_color(color);
}

void Engine::set_sky_enabled(bool enabled)
{
    if (!impl_) return;
    impl_->sky_enabled = enabled && impl_->sky.valid();
}

bool Engine::sky_enabled() const
{
    return impl_ && impl_->sky_enabled;
}

void Engine::set_sun_direction(const Vec3& direction, float intensity)
{
    if (!impl_) return;
    impl_->sky.set_sun(direction, intensity);
}

const Color& Engine::clear_color() const
{
    static const Color default_color{0.055f, 0.062f, 0.078f, 1.0f};
    return impl_ ? impl_->renderer.clear_color() : default_color;
}

void Engine::set_skin_enabled(bool enabled)
{
    if (impl_) impl_->skin_enabled = enabled;
}

bool Engine::skin_enabled() const
{
    return impl_ ? impl_->skin_enabled : true;
}

float Engine::aspect_ratio() const
{
    return impl_ ? impl_->renderer.aspect_ratio() : 1.0f;
}

double Engine::time() const
{
    return impl_ ? impl_->elapsed : 0.0;
}

float Engine::delta_time() const
{
    return impl_ ? impl_->delta_time : 0.0f;
}

std::uint64_t Engine::frame_index() const
{
    return impl_ ? impl_->frame_index : 0;
}

bool Engine::capture_screenshot(const std::string& path)
{
    if (!impl_ || !impl_->initialized)
    {
        log_message("capture_screenshot: engine is not initialized");
        return false;
    }

    std::vector<u8> rgba;
    u32 width = 0;
    u32 height = 0;
    if (!impl_->renderer.capture_frame(rgba, width, height))
    {
        log_message("capture_screenshot: back buffer readback failed");
        return false;
    }

    impl_->last_frame_rgba = std::move(rgba);
    impl_->last_frame_width = width;
    impl_->last_frame_height = height;

    if (!stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, impl_->last_frame_rgba.data(),
                        static_cast<int>(width * 4u)))
    {
        log_message("capture_screenshot: failed to write " + path);
        return false;
    }
    log_message("screenshot saved: " + path + " (" + std::to_string(width) + "x" +
                std::to_string(height) + ")");
    return true;
}

bool Engine::validate_last_frame(const Color& expected_clear)
{
    if (!impl_ || !impl_->initialized)
    {
        log_message("validate_last_frame: engine is not initialized");
        return false;
    }

    const std::vector<u8>& rgba = impl_->last_frame_rgba;
    const u32 width = impl_->last_frame_width;
    const u32 height = impl_->last_frame_height;
    if (rgba.empty() || width == 0 || height == 0)
    {
        log_message("validate_last_frame: no frame captured yet");
        return false;
    }

    const u8 clear_r = static_cast<u8>(std::round(expected_clear.r * 255.0f));
    const u8 clear_g = static_cast<u8>(std::round(expected_clear.g * 255.0f));
    const u8 clear_b = static_cast<u8>(std::round(expected_clear.b * 255.0f));
    const u8 clear_a = static_cast<u8>(std::round(expected_clear.a * 255.0f));

    std::size_t differing_pixels = 0;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixel_count; ++i)
    {
        const std::size_t base = i * 4u;
        if (rgba[base + 0] != clear_r || rgba[base + 1] != clear_g ||
            rgba[base + 2] != clear_b || rgba[base + 3] != clear_a)
        {
            ++differing_pixels;
        }
    }

    log_message("validate_last_frame: " + std::to_string(differing_pixels) + " of " +
                std::to_string(pixel_count) + " pixels differ from clear color");
    return differing_pixels > 0;
}

platform::Window* Engine::window()
{
    return impl_ ? impl_->window.get() : nullptr;
}

platform::Renderer* Engine::renderer()
{
    return impl_ ? &impl_->renderer : nullptr;
}

const std::vector<std::string>& Engine::asset_search_paths() const
{
    static const std::vector<std::string> empty;
    return impl_ ? impl_->search_paths : empty;
}

// ---------------------------------------------------------------------------
// Model public API
// ---------------------------------------------------------------------------

const std::string& Model::name() const
{
    static const std::string empty;
    return data_ ? data_->name : empty;
}

std::size_t Model::mesh_count() const
{
    return data_ ? data_->meshes.size() : 0;
}

std::size_t Model::animation_count() const
{
    return data_ ? data_->animations.size() : 0;
}

std::string Model::animation_name(std::size_t index) const
{
    if (!data_ || index >= data_->animations.size()) return {};
    return data_->animations[index].name;
}

Bounds Model::bounds() const
{
    return data_ ? data_->bounds : Bounds{};
}

void Model::play(std::size_t animation_index, bool loop)
{
    if (data_) data_->play(static_cast<int>(animation_index), loop);
}

bool Model::play(const std::string& animation_name, bool loop)
{
    if (!data_) return false;
    for (std::size_t i = 0; i < data_->animations.size(); ++i)
    {
        if (data_->animations[i].name == animation_name)
        {
            data_->play(static_cast<int>(i), loop);
            return true;
        }
    }
    return false;
}

void Model::stop()
{
    if (data_) data_->stop();
}

void Model::update(float delta_time)
{
    if (data_) data_->update(delta_time);
}

void Model::set_animation_time(float time)
{
    if (!data_) return;
    data_->animation_time = time;
    data_->needs_evaluation = true;
}

float Model::animation_time() const
{
    return data_ ? data_->animation_time : 0.0f;
}

void Model::set_animation_speed(float speed)
{
    if (data_) data_->animation_speed = speed;
}

float Model::animation_speed() const
{
    return data_ ? data_->animation_speed : 1.0f;
}

bool Model::is_playing() const
{
    return data_ && data_->playing;
}

int Model::current_animation() const
{
    return data_ ? data_->current_animation : -1;
}

} // namespace insland
