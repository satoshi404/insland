// ---------------------------------------------------------------------------
// Insland Engine - KHR_draco_mesh_compression decoder
// ---------------------------------------------------------------------------

#include "assets/draco_loader.hpp"

#include "core/logger.hpp"

#include "draco/attributes/point_attribute.h"
#include "draco/compression/decode.h"
#include "draco/core/decoder_buffer.h"
#include "draco/mesh/mesh.h"

#include <algorithm>
#include <cstring>

namespace insland::draco_loader
{

namespace
{

const draco::PointAttribute* find_attribute(const draco::Mesh& mesh,
                                            const std::vector<AttributeBinding>& bindings,
                                            const char* name,
                                            draco::GeometryAttribute::Type fallback_type)
{
    for (const AttributeBinding& binding : bindings)
    {
        if (binding.name != name) continue;
        if (const draco::PointAttribute* attribute = mesh.GetAttributeByUniqueId(binding.unique_id))
            return attribute;
    }
    return mesh.GetNamedAttribute(fallback_type);
}

/// Reads `components` floats per point.  Draco may store the attribute with a
/// different component count (or quantised integer type); ConvertValue applies
/// the type conversion and dequantisation transform.
void read_attribute(const draco::Mesh& mesh, const draco::PointAttribute* attribute,
                    int components, std::vector<float>& out)
{
    out.clear();
    if (!attribute) return;

    out.assign(static_cast<std::size_t>(mesh.num_points()) * static_cast<std::size_t>(components), 0.0f);

    const int available = static_cast<int>(attribute->num_components());
    const int count = std::min(components, available);
    if (count <= 0) return;

    for (std::uint32_t i = 0; i < mesh.num_points(); ++i)
    {
        float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const draco::AttributeValueIndex value_index = attribute->mapped_index(draco::PointIndex(i));
        if (!attribute->ConvertValue<float>(value_index, static_cast<int8_t>(count), values)) continue;

        float* target = &out[static_cast<std::size_t>(i) * static_cast<std::size_t>(components)];
        for (int c = 0; c < count; ++c) target[c] = values[c];
    }
}

} // namespace

bool decode(const std::uint8_t* data, std::size_t size,
            const std::vector<AttributeBinding>& bindings, DecodedPrimitive& out)
{
    out = DecodedPrimitive{};
    if (!data || size == 0) return false;

    draco::DecoderBuffer buffer;
    buffer.Init(reinterpret_cast<const char*>(data), size);

    draco::Decoder decoder;
    auto result = decoder.DecodeMeshFromBuffer(&buffer);
    if (!result.ok())
    {
        Log::Warn("Draco", "decode failed: ", result.status().error_msg_string());
        return false;
    }

    std::unique_ptr<draco::Mesh> mesh = std::move(result).value();
    if (!mesh || mesh->num_points() == 0) return false;

    out.vertex_count = mesh->num_points();

    read_attribute(*mesh, find_attribute(*mesh, bindings, "POSITION", draco::GeometryAttribute::POSITION),
                   3, out.positions);
    read_attribute(*mesh, find_attribute(*mesh, bindings, "NORMAL", draco::GeometryAttribute::NORMAL),
                   3, out.normals);
    read_attribute(*mesh, find_attribute(*mesh, bindings, "TEXCOORD_0", draco::GeometryAttribute::TEX_COORD),
                   2, out.uvs);
    read_attribute(*mesh, find_attribute(*mesh, bindings, "TANGENT", draco::GeometryAttribute::GENERIC),
                   4, out.tangents);
    read_attribute(*mesh, find_attribute(*mesh, bindings, "JOINTS_0", draco::GeometryAttribute::GENERIC),
                   4, out.joints);
    read_attribute(*mesh, find_attribute(*mesh, bindings, "WEIGHTS_0", draco::GeometryAttribute::GENERIC),
                   4, out.weights);

    if (out.positions.size() != out.vertex_count * 3)
    {
        Log::Warn("Draco", "decoded mesh has no POSITION attribute");
        return false;
    }

    out.indices.reserve(static_cast<std::size_t>(mesh->num_faces()) * 3);
    for (std::uint32_t face_index = 0; face_index < mesh->num_faces(); ++face_index)
    {
        const draco::Mesh::Face& corners = mesh->face(draco::FaceIndex(face_index));
        for (int c = 0; c < 3; ++c) out.indices.push_back(corners[c].value());
    }

    if (out.indices.size() < 3) return false;

    out.valid = true;
    return true;
}

} // namespace insland::draco_loader
