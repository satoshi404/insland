#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - logging (brought over from the AetherEngine foundation)
// ---------------------------------------------------------------------------
// Thread-safe, category based logging with a tiny stream-style API:
//
//      Log::Info("Renderer", "using ", backend_name, " (Vulkan 1.3)");
//      Log::SetLevel(LogLevel::Debug);
//      Log::SetFile(fs::join(fs::log_directory(), "session.log"));
//
// The message is only formatted when the level is enabled, so hot paths can
// log freely.  Severe messages are also flushed to the OS debugger on Windows.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace insland
{

enum class LogLevel : std::uint8_t
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

namespace Log
{

void SetLevel(LogLevel level) noexcept;
LogLevel GetLevel() noexcept;

bool IsEnabled(LogLevel level) noexcept;

/// Mirrors console output into a log file.  Returns false when the file cannot
/// be opened.  An empty path closes the file.
bool SetFile(const std::string& path);
void CloseFile();

/// Console/log mirrors are disabled for quiet builds (tests).
void SetQuiet(bool quiet) noexcept;

void Write(LogLevel level, std::string_view category, std::string_view message);

namespace Detail
{

inline void Append(std::ostringstream&) {}

template <typename T, typename... Rest>
void Append(std::ostringstream& stream, T&& value, Rest&&... rest)
{
    stream << std::forward<T>(value);
    Append(stream, std::forward<Rest>(rest)...);
}

template <typename... Args>
std::string Format(Args&&... args)
{
    std::ostringstream stream;
    Append(stream, std::forward<Args>(args)...);
    return std::move(stream).str();
}

} // namespace Detail

template <typename... Args>
void Trace(std::string_view category, Args&&... args)
{
    if (IsEnabled(LogLevel::Trace))
        Write(LogLevel::Trace, category, Detail::Format(std::forward<Args>(args)...));
}

template <typename... Args>
void Debug(std::string_view category, Args&&... args)
{
    if (IsEnabled(LogLevel::Debug))
        Write(LogLevel::Debug, category, Detail::Format(std::forward<Args>(args)...));
}

template <typename... Args>
void Info(std::string_view category, Args&&... args)
{
    if (IsEnabled(LogLevel::Info))
        Write(LogLevel::Info, category, Detail::Format(std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::string_view category, Args&&... args)
{
    if (IsEnabled(LogLevel::Warn))
        Write(LogLevel::Warn, category, Detail::Format(std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::string_view category, Args&&... args)
{
    if (IsEnabled(LogLevel::Error))
        Write(LogLevel::Error, category, Detail::Format(std::forward<Args>(args)...));
}

template <typename... Args>
void Critical(std::string_view category, Args&&... args)
{
    if (IsEnabled(LogLevel::Critical))
        Write(LogLevel::Critical, category, Detail::Format(std::forward<Args>(args)...));
}

} // namespace Log
} // namespace insland
