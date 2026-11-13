#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - filesystem helpers
// ---------------------------------------------------------------------------
// Thin, cross-platform helpers used by the asset loader.  Paths are handled as
// UTF-8 std::string at the API boundary and converted to std::filesystem::path
// internally (which is wide on Windows and narrow on POSIX/Android).
// ---------------------------------------------------------------------------

#include "core/types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#elif defined(__linux__)
#    include <unistd.h>
#endif

namespace insland::fs
{

namespace stdfs = std::filesystem;

inline stdfs::path to_path(std::string_view utf8)
{
#if defined(_WIN32)
    // u8path understands UTF-8 independent of the current code page.
    return stdfs::u8path(utf8.begin(), utf8.end());
#else
    return stdfs::path(std::string(utf8));
#endif
}

inline std::string to_utf8(const stdfs::path& path)
{
#if defined(_WIN32)
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return path.string();
#endif
}

inline bool exists(std::string_view path)
{
    std::error_code ec;
    return stdfs::exists(to_path(path), ec);
}

inline bool is_directory(std::string_view path)
{
    std::error_code ec;
    return stdfs::is_directory(to_path(path), ec);
}

inline bool is_regular_file(std::string_view path)
{
    std::error_code ec;
    return stdfs::is_regular_file(to_path(path), ec);
}

inline std::uint64_t file_size(std::string_view path)
{
    std::error_code ec;
    const auto size = stdfs::file_size(to_path(path), ec);
    return ec ? 0 : static_cast<std::uint64_t>(size);
}

inline std::string filename(std::string_view path)
{
    return to_utf8(to_path(path).filename());
}

inline std::string stem(std::string_view path)
{
    return to_utf8(to_path(path).stem());
}

inline std::string extension(std::string_view path)
{
    std::string ext = to_utf8(to_path(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

inline std::string parent(std::string_view path)
{
    return to_utf8(to_path(path).parent_path());
}

inline std::string join(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    return to_utf8(to_path(a) / to_path(b));
}

inline std::string absolute(std::string_view path)
{
    std::error_code ec;
    const auto result = stdfs::absolute(to_path(path), ec);
    return ec ? std::string(path) : to_utf8(result);
}

inline std::string normalized(std::string_view path)
{
    std::error_code ec;
    const auto result = stdfs::weakly_canonical(to_path(path), ec);
    return ec ? std::string(path) : to_utf8(result);
}

inline std::string current_directory()
{
    std::error_code ec;
    const auto result = stdfs::current_path(ec);
    return ec ? std::string(".") : to_utf8(result);
}

inline std::string executable_directory()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (len == 0 || len == std::size(buffer)) return current_directory();
    return parent(to_utf8(stdfs::path(std::wstring(buffer, len))));
#elif defined(__linux__)
    char buffer[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) return current_directory();
    return parent(std::string(buffer, static_cast<std::size_t>(len)));
#else
    return current_directory();
#endif
}

// Full path of the running binary ("" when not available).
inline std::string executable_path()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (len == 0 || len == std::size(buffer)) return {};
    return to_utf8(stdfs::path(std::wstring(buffer, len)));
#elif defined(__linux__)
    char buffer[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) return {};
    return std::string(buffer, static_cast<std::size_t>(len));
#else
    return {};
#endif
}

inline std::vector<u8> read_file(std::string_view path)
{
    std::ifstream file(to_path(path), std::ios::binary | std::ios::ate);
    if (!file) return {};

    const std::streamoff size = file.tellg();
    if (size < 0) return {};

    std::vector<u8> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!data.empty())
    {
        file.read(reinterpret_cast<char*>(data.data()), size);
        if (!file) return {};
    }
    return data;
}

inline std::optional<std::string> read_text(std::string_view path)
{
    std::ifstream file(to_path(path), std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;

    const std::streamoff size = file.tellg();
    if (size < 0) return std::nullopt;

    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0, std::ios::beg);
    if (!text.empty())
    {
        file.read(text.data(), size);
        if (!file) return std::nullopt;
    }
    return text;
}

inline bool write_file(std::string_view path, const void* data, std::size_t size)
{
    const stdfs::path p = to_path(path);
    std::error_code ec;
    if (p.has_parent_path()) stdfs::create_directories(p.parent_path(), ec);

    std::ofstream file(p, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    if (size > 0 && data)
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
}

inline bool write_file(std::string_view path, const std::vector<u8>& data)
{
    return write_file(path, data.data(), data.size());
}

inline bool write_text(std::string_view path, std::string_view text)
{
    return write_file(path, text.data(), text.size());
}

// Search for a file in a list of directories.  Absolute paths are checked
// directly.  Returns an empty optional if nothing was found.
inline std::optional<std::string> find(std::string_view name, const std::vector<std::string>& search_dirs)
{
    const stdfs::path p = to_path(name);
    if (p.is_absolute())
        return exists(name) ? std::optional<std::string>(std::string(name)) : std::nullopt;

    std::error_code ec;
    if (stdfs::exists(p, ec)) return std::string(name);

    for (const auto& dir : search_dirs)
    {
        const std::string candidate = join(dir, std::string(name));
        if (exists(candidate)) return candidate;
    }
    return std::nullopt;
}

inline std::vector<std::string> list_files(std::string_view directory, bool recursive = false)
{
    std::vector<std::string> files;
    std::error_code ec;
    const auto root = to_path(directory);
    if (!stdfs::exists(root, ec)) return files;

    if (recursive)
    {
        for (const auto& entry : stdfs::recursive_directory_iterator(root, ec))
            if (entry.is_regular_file()) files.push_back(to_utf8(entry.path()));
    }
    else
    {
        for (const auto& entry : stdfs::directory_iterator(root, ec))
            if (entry.is_regular_file()) files.push_back(to_utf8(entry.path()));
    }
    return files;
}

// ---------------------------------------------------------------------------
// Standard paths
// ---------------------------------------------------------------------------
// Regions of a game install, following the pattern used by professional
// engines:
//
//   package    read-only data shipped with the build (use for assets)
//   assets     <package>/assets, the content root
//   user       writable per-user data (configs and saves go here)
//   temp       scratch space, may be deleted at any time
//
// init() is cheap and may be called several times; the roots are resolved
// once.  Every writable location is created eagerly so that write_file()
// succeeds later without extra checks.
// ---------------------------------------------------------------------------

struct Paths
{
    std::string executable;             // path of the running binary
    std::string executable_directory;   // e.g. build/
    std::string working_directory;      // where the process was started

    std::string package_root;           // directory that contains assets/ etc.
    std::string assets;                 // <package_root>/assets

    std::string user;                   // writable user data root
    std::string user_config;            // settings files
    std::string user_save;              // save games and render captures
    std::string user_log;               // crash dumps and debug logs
    std::string temp;                   // scratch space
};

inline Paths& standard_paths()
{
    static Paths paths;
    return paths;
}

inline std::string config_directory()     { return standard_paths().user_config; }
inline std::string save_directory()       { return standard_paths().user_save; }
inline std::string log_directory()        { return standard_paths().user_log; }
inline std::string temporary_directory()  { return standard_paths().temp; }

inline void ensure_directories(Paths& paths)
{
    std::error_code ec;
    if (!paths.user.empty())
    {
        stdfs::create_directories(to_path(paths.user_config), ec);
        stdfs::create_directories(to_path(paths.user_save), ec);
        stdfs::create_directories(to_path(paths.user_log), ec);
        stdfs::create_directories(to_path(paths.temp), ec);
    }
}

inline bool init_standard_paths(const std::string& engine_asset_root)
{
    Paths& paths = standard_paths();

    if (paths.executable.empty())
    {
        paths.executable = executable_path();
        paths.executable_directory = executable_directory();
        paths.working_directory = current_directory();

        // Package layout: prefer "workspace/game/assets" next to the build
        // directory (development), otherwise the assets folder sits beside
        // the executable (shipped layout).
        const std::string dev_assets = normalized(join(paths.executable_directory, "../workspace/game/assets"));
        if (is_directory(dev_assets))
        {
            paths.package_root = parent(dev_assets);
            paths.assets = dev_assets;
        }
        else
        {
            paths.package_root = paths.executable_directory;
            paths.assets = join(paths.executable_directory, "assets");
        }

#if defined(_WIN32)
        const std::string appdata = []() -> std::string {
            char buffer[MAX_PATH] = {};
            if (GetEnvironmentVariableA("APPDATA", buffer, MAX_PATH) == 0) return {};
            return std::string(buffer);
        }();
        paths.user = appdata.empty() ? join(paths.executable_directory, "user")
                                     : normalized(join(appdata, "Insland"));
#elif defined(__ANDROID__)
        paths.user = "/storage/emulated/0/Android/data/insland/files";
#else
        const char* home = ::getenv("HOME");
        paths.user = home ? normalized(std::string(home) + "/.local/share/insland") : std::string("/tmp/insland");
#endif
        paths.user_config = join(paths.user, "config");
        paths.user_save = join(paths.user, "save");
        paths.user_log = join(paths.user, "log");
        paths.temp = join(paths.user, "temp");
    }

    if (!engine_asset_root.empty())
        paths.assets = normalized(engine_asset_root);

    ensure_directories(paths);
    return true;
}

// Resolves a relative asset path (<package>/assets + user supplied roots).
// Returns "" when the file does not exist anywhere.
inline std::string locate_asset(std::string_view relative_path, const std::vector<std::string>& extra_roots = {})
{
    if (relative_path.empty()) return {};
    if (exists(relative_path)) return std::string(relative_path);

    const Paths& paths = standard_paths();
    std::vector<std::string> roots;
    roots.reserve(extra_roots.size() + 2);
    for (const auto& root : extra_roots) roots.push_back(root);
    roots.push_back(paths.assets);
    roots.push_back(paths.package_root);

    const auto found = find(relative_path, roots);
    return found ? *found : std::string{};
}

} // namespace insland::fs
