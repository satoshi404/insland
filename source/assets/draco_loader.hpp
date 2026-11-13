#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - KHR_draco_mesh_compression support
// ---------------------------------------------------------------------------
// glTF stores Draco compressed primitives as a blob referenced by a bufferView
// plus a table that maps the glTF attribute names ("POSITION", "NORMAL", ...)
// to Draco attribute unique ids:
//
//     "extensions": {
//       "KHR_draco_mesh_compression": {
//         "bufferView": 7,
//         "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }
//       }
//     }
//
// The tinygltf v3 parser keeps extensions as generic values, so engine.cpp
// walks that tree and hands the mapping to decode().  Decoded arrays use the
// same layout the regular accessor path produces (flat floats, 3 components
// for positions/normals, 2 for UVs, 4 for tangents/joints/weights).
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace insland::draco_loader
{

struct AttributeBinding
{
    std::string name;      // glTF attribute name, e.g. "POSITION"
    std::uint32_t unique_id = 0; // Draco attribute unique id
};

struct DecodedPrimitive
{
    std::vector<float> positions;  // 3 floats / vertex
    std::vector<float> normals;    // 3 floats / vertex
    std::vector<float> uvs;        // 2 floats / vertex
    std::vector<float> tangents;   // 4 floats / vertex
    std::vector<float> joints;     // 4 floats / vertex
    std::vector<float> weights;    // 4 floats / vertex
    std::vector<std::uint32_t> indices;

    std::size_t vertex_count = 0;
    bool valid = false;
};

/// Decodes a Draco mesh blob.  `bindings` describes which glTF attribute each
/// Draco attribute id maps to.  Returns false when the blob is not a valid
/// Draco mesh or does not contain a position attribute.
bool decode(const std::uint8_t* data, std::size_t size,
            const std::vector<AttributeBinding>& bindings, DecodedPrimitive& out);

} // namespace insland::draco_loader
