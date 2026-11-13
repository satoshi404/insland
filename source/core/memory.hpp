#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - memory tracker
// ---------------------------------------------------------------------------
// A tiny, thread-safe accounting layer so games and the demo UI can show where
// memory goes:
//
//      Memory::Stats stats = Memory::stats();
//      Log::Info("Memory", Memory::summary());
//
// Subsystems report their usage explicitly (GPU buffers, textures, meshes,
// animation data, terrain chunks...), which keeps the hot path free of hooks
// while still exposing a single overview panel.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>

namespace insland::Memory
{

enum class Category : std::uint8_t
{
    Meshes,
    Textures,
    Animations,
    Terrain,
    UI,
    GeometryStaging,
    Count
};

struct Entry
{
    std::size_t bytes = 0;
    std::uint32_t allocations = 0;
};

void add(Category category, std::size_t bytes, std::uint32_t allocations = 1);
void remove(Category category, std::size_t bytes, std::uint32_t allocations = 1);

const char* category_name(Category category);
Entry entry(Category category);
Entry total();

std::string summary();

} // namespace insland::Memory
