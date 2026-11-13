#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - GPU pipeline
// ---------------------------------------------------------------------------
// The engine uses one vertex format and one pipeline for every mesh.  Skinning
// is selected at draw time by a uniform flag; non-skinned draws simply ignore
// the bone array.  This keeps the material/mesh code tiny while still allowing
// animated glTF models.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

#include "DeviceContext.h"
#include "PipelineState.h"
#include "Buffer.h"
#include "TextureView.h"
#include "Sampler.h"

#include "core/types.hpp"
#include "platform/renderer.hpp"

namespace insland
{

// The bone palette is split across several constant buffers.  GLES 3.0 only
// guarantees MAX_UNIFORM_BLOCK_SIZE == 16 KB, so a single 512-bone block
// (512 * 64 B = 32 KB) would fail to link on many Android GPUs.  Four blocks of
// 128 bones are 8 KB each and fit comfortably everywhere.
constexpr std::uint32_t kMaxBones = 512;
constexpr std::uint32_t kBonesPerBlock = 128;
constexpr std::uint32_t kBoneBlockCount = kMaxBones / kBonesPerBlock;

static_assert(kBoneBlockCount * kBonesPerBlock == kMaxBones, "bone blocks must cover the palette");

struct Vertex
{
    Vec3 position{0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    Vec2 uv{0.0f};
    Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    Vec4 joints{0.0f};   // stored as floats so the same layout works on D3D and GL
    Vec4 weights{0.0f};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};  // per-vertex tint (procedural terrain, particles)
};

static_assert(sizeof(Vertex) == 96, "Vertex layout changed: update the input layout");

struct Uniforms
{
    alignas(16) Mat4 view_proj{1.0f};
    alignas(16) Mat4 model{1.0f};
    alignas(16) Mat4 normal_matrix{1.0f};
    alignas(16) Vec4 base_color{1.0f};
    alignas(16) Vec4 params{0.0f};      // x=use texture, y=skinned, z=metallic, w=roughness
    alignas(16) Vec4 camera_position{0.0f};
    alignas(16) Vec4 light_direction{0.4f, -1.0f, -0.6f, 0.0f};
    alignas(16) Vec4 light_color{1.0f, 1.0f, 1.0f, 1.0f};
};

// One slice of the bone palette (see kMaxBones).  The shader exposes these as
// cbBones0..cbBones3.
struct BoneBlock
{
    alignas(16) Mat4 bones[kBonesPerBlock];
};

static_assert(sizeof(BoneBlock) == kBonesPerBlock * sizeof(Mat4), "unexpected bone block padding");

class MeshPipeline
{
public:
    MeshPipeline() = default;
    ~MeshPipeline() { shutdown(); }

    MeshPipeline(const MeshPipeline&) = delete;
    MeshPipeline& operator=(const MeshPipeline&) = delete;

    bool create(platform::Renderer& renderer);
    void shutdown();

    bool valid() const { return pso_ != nullptr; }

    void draw(platform::Renderer& renderer, const Uniforms& uniforms,
              const Mat4* bones, std::uint32_t bone_count,
              Diligent::ITextureView* texture, Diligent::ISampler* sampler,
              Diligent::IBuffer* vertex_buffer, Diligent::IBuffer* index_buffer,
              std::uint32_t index_count);

private:
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constant_buffer_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> bone_buffers_[kBoneBlockCount];

    Diligent::IShaderResourceVariable* cb_variable_ = nullptr;
    Diligent::IShaderResourceVariable* bone_variables_[kBoneBlockCount] = {};
    Diligent::IShaderResourceVariable* texture_variable_ = nullptr;
    Diligent::IShaderResourceVariable* sampler_variable_ = nullptr;
};

} // namespace insland
