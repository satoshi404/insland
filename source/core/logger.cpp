// ---------------------------------------------------------------------------
// Insland Engine - logging implementation (brought over from AetherEngine)
// ---------------------------------------------------------------------------

#include "core/logger.hpp"
#include "core/filesystem.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    if defined(_MSC_VER)
#        include <io.h>
#    endif
#else
#    include <unistd.h>
#endif

namespace insland::Log
{
namespace
{

std::atomic<LogLevel> g_level{LogLevel::Info};
std::atomic<bool> g_quiet{false};
std::mutex g_mutex;
std::ofstream g_file;
bool g_file_open = false;

const char* level_name(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "FATAL";
        case LogLevel::Off: return "OFF";
    }
    return "?";
}

const char* level_color(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace: return "\x1b[90m";
        case LogLevel::Debug: return "\x1b[36m";
        case LogLevel::Info:  return "\x1b[32m";
        case LogLevel::Warn:  return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
        case LogLevel::Critical: return "\x1b[1;31m";
        case LogLevel::Off: return "";
    }
    return "";
}

bool is_tty(std::FILE* stream) noexcept
{
#if defined(_WIN32) && defined(_MSC_VER)
    return _isatty(_fileno(stream)) != 0;
#else
    return ::isatty(::fileno(stream)) != 0;
#endif
}

std::string timestamp()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t seconds = system_clock::to_time_t(now);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);

    char result[48]{};
    std::snprintf(result, sizeof(result), "%s.%03d", buffer, static_cast<int>(ms.count()));
    return result;
}

void write_line(std::FILE* stream, const std::string& line, LogLevel level, bool color)
{
    if (color)
        std::fprintf(stream, "%s%s\x1b[0m\n", level_color(level), line.c_str());
    else
        std::fprintf(stream, "%s\n", line.c_str());
    std::fflush(stream);
}

} // namespace

void SetLevel(LogLevel level) noexcept
{
    g_level.store(level, std::memory_order_relaxed);
}

LogLevel GetLevel() noexcept
{
    return g_level.load(std::memory_order_relaxed);
}

bool IsEnabled(LogLevel level) noexcept
{
    return static_cast<int>(level) >= static_cast<int>(g_level.load(std::memory_order_relaxed)) &&
           g_level.load(std::memory_order_relaxed) != LogLevel::Off;
}

void SetQuiet(bool quiet) noexcept
{
    g_quiet.store(quiet, std::memory_order_relaxed);
}

bool SetFile(const std::string& path)
{
    CloseFile();

    if (path.empty()) return true;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_file.open(path.c_str(), std::ios::out | std::ios::trunc);
    g_file_open = static_cast<bool>(g_file);
    return g_file_open;
}

void CloseFile()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file_open)
    {
        g_file.flush();
        g_file.close();
        g_file_open = false;
    }
}

void Write(LogLevel level, std::string_view category, std::string_view message)
{
    if (!IsEnabled(level) || g_quiet.load(std::memory_order_relaxed)) return;

    using clock = std::chrono::steady_clock;
    static const clock::time_point boot_time = clock::now();
    const double seconds = std::chrono::duration<double>(clock::now() - boot_time).count();

    char header[64]{};
    std::snprintf(header, sizeof(header), "[%10.3f] [%-5s] [%.*s] ", seconds, level_name(level),
                  static_cast<int>(category.size()), category.data());
    const std::string line = std::string(header) + std::string(message);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file_open)
    {
        g_file << line << '\n';
        g_file.flush();
    }
    else if (std::FILE* stream = (static_cast<int>(level) >= static_cast<int>(LogLevel::Error)) ? stderr : stdout)
    {
        write_line(stream, line, level, is_tty(stream));
    }

#if defined(_WIN32)
    if (static_cast<int>(level) >= static_cast<int>(LogLevel::Warn))
        OutputDebugStringA((line + "\n").c_str());
#endif
}

} // namespace insland::Log
