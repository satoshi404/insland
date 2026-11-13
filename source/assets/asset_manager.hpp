#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - asset manager (adapted from the AetherEngine foundation)
// ---------------------------------------------------------------------------
// File-based asset access with a small asynchronous API:
//
//     insland::AssetManager assets(fs::standard_paths().assets);
//     if (assets.exists("models/tree.glb")) ...
//     auto data  = assets.read("models/tree.glb");            // synchronous
//     auto future = assets.read_async("models/tree.glb");      // asynchronous
//
// The asynchronous path uses std::async today and is intentionally isolated
// behind this class; a dedicated job system can replace it later without
// touching game code.  On Android, package assets are not regular files;
// typed loaders will route them through AAssetManager while this class keeps
// handling user files.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <future>
#include <string>
#include <string_view>
#include <vector>

namespace insland
{

struct AssetData
{
    std::vector<std::byte> bytes;
    std::string error;

    bool ok() const { return error.empty(); }
};

class AssetManager
{
public:
    AssetManager() = default;
    explicit AssetManager(std::string root);

    void set_root(std::string root);
    const std::string& root() const { return root_; }

    bool exists(std::string_view relative_path) const;

    /// Resolves relative paths (and bare names) against the manager root and
    /// the engine standard asset roots.  Empty on failure.
    std::string locate(std::string_view relative_path) const;

    AssetData read(std::string_view relative_path) const;

    /// File size in bytes, 0 when missing.
    std::size_t size(std::string_view relative_path) const;

    std::future<AssetData> read_async(std::string_view relative_path) const;

private:
    std::string root_;
};

} // namespace insland
