// ---------------------------------------------------------------------------
// Insland Engine - asset manager implementation (adapted from AetherEngine)
// ---------------------------------------------------------------------------

#include "assets/asset_manager.hpp"

#include "core/filesystem.hpp"
#include "core/logger.hpp"

#include <cstring>

namespace insland
{

AssetManager::AssetManager(std::string root) : root_(std::move(root)) {}

void AssetManager::set_root(std::string root)
{
    root_ = std::move(root);
}

bool AssetManager::exists(std::string_view relative_path) const
{
    return !locate(relative_path).empty();
}

std::string AssetManager::locate(std::string_view relative_path) const
{
    const std::string path(relative_path);
    if (path.empty()) return {};

    if (fs::exists(path) && !fs::is_directory(path))
        return path;

    if (!root_.empty())
    {
        const std::string joined = fs::join(root_, path);
        if (fs::exists(joined) && !fs::is_directory(joined))
            return joined;

        // Bare name: fall back to a recursive scan of the root.
        if (fs::filename(path) == path)
        {
            for (const std::string& file : fs::list_files(root_, true))
                if (fs::filename(file) == path) return file;
        }
    }

    const std::string located = fs::locate_asset(path);
    return located.empty() || fs::is_directory(located) ? std::string{} : located;
}

AssetData AssetManager::read(std::string_view relative_path) const
{
    AssetData data;

    const std::string resolved = locate(relative_path);
    if (resolved.empty())
    {
        data.error = "asset not found: " + std::string(relative_path);
        Log::Warn("Assets", data.error);
        return data;
    }

    const std::vector<std::uint8_t> raw = fs::read_file(resolved);
    if (raw.empty())
    {
        data.error = "failed to read asset: " + resolved;
        Log::Warn("Assets", data.error);
        return data;
    }

    data.bytes.resize(raw.size());
    std::memcpy(data.bytes.data(), raw.data(), raw.size());
    Log::Trace("Assets", "read ", resolved, " (", raw.size(), " bytes)");
    return data;
}

std::size_t AssetManager::size(std::string_view relative_path) const
{
    const std::string resolved = locate(relative_path);
    if (resolved.empty()) return 0;
    return static_cast<std::size_t>(fs::file_size(resolved));
}

std::future<AssetData> AssetManager::read_async(std::string_view relative_path) const
{
    const std::string path(relative_path);
    return std::async(std::launch::async, [this, path] { return read(path); });
}

} // namespace insland
