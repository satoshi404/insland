#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - JSON serializer
// ---------------------------------------------------------------------------
// A small, dependency-free JSON document model with a strict parser and a
// deterministic writer.  It is used by the scene format, but any game data can
// be stored with it:
//
//     JsonValue doc = JsonValue::object();
//     doc["name"] = "island";
//     doc["seed"] = 7;
//     doc["objects"].push(insland::to_json(transform));
//     insland::save_document("scene.json", doc);
//
//     if (auto loaded = insland::load_document("scene.json"))
//     {
//         const int seed = (*loaded)["seed"].as_int();
//     }
//
// Design notes
//   * values are self-contained; copies are deep copies (a document can be
//     moved or returned by value without dangling references)
//   * objects keep insertion order, so files diff nicely between runs
//   * numbers are stored as double and printed with enough precision for float
//   * save_document() writes to a temporary file and renames it, so a crash
//     during a save never leaves a half-written document behind
//   * documents carry a "format"/"version" header; loading a newer file fails
//     with a clear message instead of silently misreading it
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/types.hpp"

namespace insland
{

enum class JsonType : std::uint8_t
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

class JsonValue
{
public:
    using Array = std::vector<JsonValue>;

    JsonValue() = default;

    JsonValue(std::nullptr_t) : type_(JsonType::Null) {}
    JsonValue(bool value) : type_(JsonType::Bool), boolean_(value) {}
    JsonValue(double value) : type_(JsonType::Number), number_(value) {}
    JsonValue(float value) : type_(JsonType::Number), number_(value), number_is_float_(true) {}
    JsonValue(std::string value) : type_(JsonType::String), string_(std::move(value)) {}
    JsonValue(const char* value) : type_(JsonType::String), string_(value ? value : "") {}

    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    JsonValue(T value) : type_(JsonType::Number), number_(static_cast<double>(value))
    {
    }

    static JsonValue array();
    static JsonValue object();

    JsonType type() const { return type_; }

    bool is_null() const { return type_ == JsonType::Null; }
    bool is_bool() const { return type_ == JsonType::Bool; }
    bool is_number() const { return type_ == JsonType::Number; }
    bool is_string() const { return type_ == JsonType::String; }
    bool is_array() const { return type_ == JsonType::Array; }
    bool is_object() const { return type_ == JsonType::Object; }

    // Scalar access.  Values of the wrong type return the supplied fallback.
    bool as_bool(bool fallback = false) const;
    double as_number(double fallback = 0.0) const;
    float as_float(float fallback = 0.0f) const;
    int as_int(int fallback = 0) const;
    std::uint32_t as_u32(std::uint32_t fallback = 0) const;
    std::string as_string(std::string_view fallback = {}) const;

    // Array access.  items() on a non-array returns an empty list.
    const Array& items() const { return array_; }
    Array& items() { return array_; }
    std::size_t size() const;
    JsonValue& push(JsonValue value);

    // Object access.  operator[] creates the object (and the member) on
    // demand, which makes building documents terse.
    bool contains(std::string_view key) const;
    const JsonValue* find(std::string_view key) const;
    JsonValue* find(std::string_view key);
    JsonValue& operator[](std::string_view key);
    const JsonValue& operator[](std::string_view key) const;
    JsonValue& set(std::string_view key, JsonValue value);
    /// Inserts (or updates) a member, keeping it at the front of the object.
    /// Used for the format/version header so hand-edited files stay readable.
    JsonValue& set_front(std::string_view key, JsonValue value);
    std::size_t member_count() const { return object_keys_.size(); }

    // Serializes to compact or indented JSON.  indent < 0 means compact.
    std::string dump(int indent = 2) const;

private:
    void write(std::string& out, int indent, int depth) const;

    JsonType type_ = JsonType::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    bool number_is_float_ = false;
    std::string string_;
    Array array_;
    std::vector<std::string> object_keys_;
    Array object_values_;
};

// ---------------------------------------------------------------------------
// Parsing and documents
// ---------------------------------------------------------------------------

// Parses a complete JSON document.  On failure returns std::nullopt and fills
// `error` with a message that includes the line and column.
std::optional<JsonValue> parse_json(std::string_view text, std::string* error = nullptr);

/// Current on-disk document version.  Increment when the meaning of a field
/// changes; load_document() rejects files written by a newer engine.
inline constexpr int kDocumentVersion = 1;
inline constexpr std::string_view kDocumentFormat = "insland";

/// Writes `document` as JSON, adding the format/version header.  The write is
/// atomic: data goes to `<path>.tmp` and is renamed over the target.
bool save_document(const std::string& path, JsonValue document, std::string* error = nullptr);

/// Reads and validates a document written by save_document().
std::optional<JsonValue> load_document(const std::string& path, std::string* error = nullptr);

// ---------------------------------------------------------------------------
// Typed conversions
// ---------------------------------------------------------------------------
// Vectors are stored as [x, y, z]; quaternions as [w, x, y, z]; colours as
// [r, g, b, a]; transforms as an object with position/rotation/scale.  The
// readers also accept objects with named components, which keeps hand-written
// files readable.
// ---------------------------------------------------------------------------

JsonValue to_json(const Vec3& value);
JsonValue to_json(const Quat& value);
JsonValue to_json(const Color& value);
JsonValue to_json(const Transform& value);

bool from_json(const JsonValue& value, Vec3& out);
bool from_json(const JsonValue& value, Quat& out);
bool from_json(const JsonValue& value, Color& out);
bool from_json(const JsonValue& value, Transform& out);

Vec3 vec3_from_json(const JsonValue& value, const Vec3& fallback = Vec3(0.0f));
Quat quat_from_json(const JsonValue& value, const Quat& fallback = Quat(1.0f, 0.0f, 0.0f, 0.0f));
Color color_from_json(const JsonValue& value, const Color& fallback = Color::white());
Transform transform_from_json(const JsonValue& value, const Transform& fallback = Transform{});

} // namespace insland
