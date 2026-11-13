// ---------------------------------------------------------------------------
// Insland Engine - memory tracker implementation
// ---------------------------------------------------------------------------

#include "core/memory.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <string>

namespace insland::Memory
{
namespace
{

std::mutex g_mutex;
using Counters = std::array<Entry, static_cast<std::size_t>(Category::Count)>;
Counters g_counters{};

const char* const kNames[] = {
    "Meshes", "Textures", "Animations", "Terrain", "UI", "Staging",
};
static_assert(std::size(kNames) == static_cast<std::size_t>(Category::Count));

} // namespace

void add(Category category, std::size_t bytes, std::uint32_t allocations)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry& entry = g_counters[static_cast<std::size_t>(category)];
    entry.bytes += bytes;
    entry.allocations += allocations;
}

void remove(Category category, std::size_t bytes, std::uint32_t allocations)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry& entry = g_counters[static_cast<std::size_t>(category)];
    entry.bytes = std::max<std::size_t>(entry.bytes, bytes) - bytes;
    entry.allocations = entry.allocations > allocations ? entry.allocations - allocations : 0;
}

const char* category_name(Category category)
{
    return kNames[static_cast<std::size_t>(category)];
}

Entry entry(Category category)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_counters[static_cast<std::size_t>(category)];
}

Entry total()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry sum;
    for (const Entry& entry : g_counters)
    {
        sum.bytes += entry.bytes;
        sum.allocations += entry.allocations;
    }
    return sum;
}

std::string summary()
{
    const Counters snapshot = []() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_counters;
    }();

    char line[128];
    std::string result = "Memory: ";
    double total = 0.0;
    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        if (snapshot[i].bytes == 0) continue;
        const double mb = static_cast<double>(snapshot[i].bytes) / (1024.0 * 1024.0);
        total += mb;
        std::snprintf(line, sizeof(line), "%s %.2f MB (%u), ", kNames[i], mb, snapshot[i].allocations);
        result += line;
    }
    std::snprintf(line, sizeof(line), "total %.2f MB", total);
    return result + line;
}

} // namespace insland::Memory
