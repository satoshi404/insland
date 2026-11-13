// ---------------------------------------------------------------------------
// Insland Engine - JSON serializer implementation
// ---------------------------------------------------------------------------

#include "core/serializer.hpp"

#include "core/filesystem.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>

namespace insland
{
namespace
{

const JsonValue& null_value()
{
    static const JsonValue value;
    return value;
}

void append_escaped(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (unsigned char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                static const char* digits = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(c >> 4) & 0xF]);
                out.push_back(digits[c & 0xF]);
            }
            else
            {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

void append_number(std::string& out, double value, bool single_precision)
{
    if (!std::isfinite(value))
    {
        out += "0";
        return;
    }

    // Print the shortest representation that round-trips through the same
    // scalar type.  Scene files are mostly floats, so 0.018f becomes "0.018"
    // instead of "0.0179999992" while still being exact.
    if (single_precision)
    {
        const float single = static_cast<float>(value);
        for (int precision = 6; precision <= std::numeric_limits<float>::max_digits10; ++precision)
        {
            std::ostringstream candidate;
            candidate << std::setprecision(precision) << single;
            const std::string text = candidate.str();
            if (std::strtof(text.c_str(), nullptr) == single)
            {
                out += text;
                return;
            }
        }
    }
    else
    {
        for (int precision = 15; precision <= std::numeric_limits<double>::max_digits10; ++precision)
        {
            std::ostringstream candidate;
            candidate << std::setprecision(precision) << value;
            const std::string text = candidate.str();
            if (std::strtod(text.c_str(), nullptr) == value)
            {
                out += text;
                return;
            }
        }
    }

    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    out += stream.str();
}

void append_indent(std::string& out, int indent, int depth)
{
    if (indent <= 0) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent * depth), ' ');
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

struct Parser
{
    std::string_view text;
    std::size_t position = 0;
    std::string error;

    bool skip_whitespace()
    {
        while (position < text.size())
        {
            const char c = text[position];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                ++position;
            else
                break;
        }
        return true;
    }

    std::string where() const
    {
        std::size_t line = 1;
        std::size_t column = 1;
        for (std::size_t i = 0; i < position && i < text.size(); ++i)
        {
            if (text[i] == '\n')
            {
                ++line;
                column = 1;
            }
            else
            {
                ++column;
            }
        }
        return "line " + std::to_string(line) + ", column " + std::to_string(column);
    }

    bool fail(std::string message)
    {
        if (error.empty()) error = std::move(message) + " (" + where() + ")";
        return false;
    }

    bool parse_value(JsonValue& out)
    {
        skip_whitespace();
        if (position >= text.size()) return fail("unexpected end of document");

        switch (text[position])
        {
        case '{': return parse_object(out);
        case '[': return parse_array(out);
        case '"':
        {
            std::string value;
            if (!parse_string(value)) return false;
            out = JsonValue(std::move(value));
            return true;
        }
        case 't':
            if (!parse_literal("true")) return false;
            out = JsonValue(true);
            return true;
        case 'f':
            if (!parse_literal("false")) return false;
            out = JsonValue(false);
            return true;
        case 'n':
            if (!parse_literal("null")) return false;
            out = JsonValue(nullptr);
            return true;
        default:
            return parse_number(out);
        }
    }

    bool parse_literal(std::string_view literal)
    {
        if (text.substr(position, literal.size()) != literal)
            return fail("invalid literal, expected '" + std::string(literal) + "'");
        position += literal.size();
        return true;
    }

    bool parse_object(JsonValue& out)
    {
        out = JsonValue::object();
        ++position; // '{'
        skip_whitespace();
        if (position < text.size() && text[position] == '}')
        {
            ++position;
            return true;
        }

        while (true)
        {
            skip_whitespace();
            if (position >= text.size() || text[position] != '"')
                return fail("expected an object key");

            std::string key;
            if (!parse_string(key)) return false;

            skip_whitespace();
            if (position >= text.size() || text[position] != ':')
                return fail("expected ':' after object key");
            ++position;

            JsonValue value;
            if (!parse_value(value)) return false;
            out.set(key, std::move(value));

            skip_whitespace();
            if (position >= text.size()) return fail("unterminated object");
            if (text[position] == ',')
            {
                ++position;
                continue;
            }
            if (text[position] == '}')
            {
                ++position;
                return true;
            }
            return fail("expected ',' or '}' in object");
        }
    }

    bool parse_array(JsonValue& out)
    {
        out = JsonValue::array();
        ++position; // '['
        skip_whitespace();
        if (position < text.size() && text[position] == ']')
        {
            ++position;
            return true;
        }

        while (true)
        {
            JsonValue value;
            if (!parse_value(value)) return false;
            out.push(std::move(value));

            skip_whitespace();
            if (position >= text.size()) return fail("unterminated array");
            if (text[position] == ',')
            {
                ++position;
                continue;
            }
            if (text[position] == ']')
            {
                ++position;
                return true;
            }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parse_hex4(std::uint32_t& value)
    {
        if (position + 4 > text.size()) return fail("truncated \\u escape");
        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text[position++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return fail("invalid hexadecimal digit in \\u escape");
        }
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t codepoint)
    {
        if (codepoint <= 0x7F)
        {
            out.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool parse_string(std::string& out)
    {
        out.clear();
        ++position; // opening quote

        while (position < text.size())
        {
            const unsigned char c = static_cast<unsigned char>(text[position++]);
            if (c == '"') return true;
            if (c < 0x20) return fail("unescaped control character in string");
            if (c != '\\')
            {
                out.push_back(static_cast<char>(c));
                continue;
            }

            if (position >= text.size()) return fail("unterminated escape sequence");
            const char escape = text[position++];
            switch (escape)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                std::uint32_t codepoint = 0;
                if (!parse_hex4(codepoint)) return false;

                // Combine UTF-16 surrogate pairs.
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF && position + 6 <= text.size() &&
                    text[position] == '\\' && text[position + 1] == 'u')
                {
                    position += 2;
                    std::uint32_t low = 0;
                    if (!parse_hex4(low)) return false;
                    if (low >= 0xDC00 && low <= 0xDFFF)
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(out, codepoint);
                break;
            }
            default:
                return fail("invalid escape sequence");
            }
        }
        return fail("unterminated string");
    }

    bool parse_number(JsonValue& out)
    {
        const std::size_t start = position;
        if (position < text.size() && text[position] == '-') ++position;

        if (position >= text.size() || !(text[position] >= '0' && text[position] <= '9'))
            return fail("invalid value");

        if (text[position] == '0')
        {
            ++position;
        }
        else
        {
            while (position < text.size() && text[position] >= '0' && text[position] <= '9')
                ++position;
        }

        if (position < text.size() && text[position] == '.')
        {
            ++position;
            if (position >= text.size() || !(text[position] >= '0' && text[position] <= '9'))
                return fail("invalid fraction in number");
            while (position < text.size() && text[position] >= '0' && text[position] <= '9')
                ++position;
        }

        if (position < text.size() && (text[position] == 'e' || text[position] == 'E'))
        {
            ++position;
            if (position < text.size() && (text[position] == '+' || text[position] == '-')) ++position;
            if (position >= text.size() || !(text[position] >= '0' && text[position] <= '9'))
                return fail("invalid exponent in number");
            while (position < text.size() && text[position] >= '0' && text[position] <= '9')
                ++position;
        }

        const std::string token(text.substr(start, position - start));
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0') return fail("invalid number");
        out = JsonValue(value);
        return true;
    }
};

float number_component(const JsonValue& object, const char* key, float fallback)
{
    const JsonValue* value = object.find(key);
    return value ? value->as_float(fallback) : fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// JsonValue
// ---------------------------------------------------------------------------

JsonValue JsonValue::array()
{
    JsonValue value;
    value.type_ = JsonType::Array;
    return value;
}

JsonValue JsonValue::object()
{
    JsonValue value;
    value.type_ = JsonType::Object;
    return value;
}

bool JsonValue::as_bool(bool fallback) const
{
    return type_ == JsonType::Bool ? boolean_ : fallback;
}

double JsonValue::as_number(double fallback) const
{
    return type_ == JsonType::Number ? number_ : fallback;
}

float JsonValue::as_float(float fallback) const
{
    return type_ == JsonType::Number ? static_cast<float>(number_) : fallback;
}

int JsonValue::as_int(int fallback) const
{
    return type_ == JsonType::Number ? static_cast<int>(number_) : fallback;
}

std::uint32_t JsonValue::as_u32(std::uint32_t fallback) const
{
    if (type_ != JsonType::Number || number_ < 0.0) return fallback;
    return static_cast<std::uint32_t>(number_);
}

std::string JsonValue::as_string(std::string_view fallback) const
{
    return type_ == JsonType::String ? string_ : std::string(fallback);
}

std::size_t JsonValue::size() const
{
    switch (type_)
    {
    case JsonType::Array: return array_.size();
    case JsonType::Object: return object_keys_.size();
    default: return 0;
    }
}

JsonValue& JsonValue::push(JsonValue value)
{
    if (type_ != JsonType::Array)
    {
        type_ = JsonType::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
    return array_.back();
}

bool JsonValue::contains(std::string_view key) const
{
    return find(key) != nullptr;
}

const JsonValue* JsonValue::find(std::string_view key) const
{
    if (type_ != JsonType::Object) return nullptr;
    for (std::size_t i = 0; i < object_keys_.size(); ++i)
        if (object_keys_[i] == key) return &object_values_[i];
    return nullptr;
}

JsonValue* JsonValue::find(std::string_view key)
{
    if (type_ != JsonType::Object) return nullptr;
    for (std::size_t i = 0; i < object_keys_.size(); ++i)
        if (object_keys_[i] == key) return &object_values_[i];
    return nullptr;
}

JsonValue& JsonValue::operator[](std::string_view key)
{
    if (JsonValue* existing = find(key)) return *existing;
    return set(key, JsonValue());
}

const JsonValue& JsonValue::operator[](std::string_view key) const
{
    if (const JsonValue* existing = find(key)) return *existing;
    return null_value();
}

JsonValue& JsonValue::set(std::string_view key, JsonValue value)
{
    if (type_ != JsonType::Object)
    {
        type_ = JsonType::Object;
        object_keys_.clear();
        object_values_.clear();
    }
    for (std::size_t i = 0; i < object_keys_.size(); ++i)
    {
        if (object_keys_[i] == key)
        {
            object_values_[i] = std::move(value);
            return object_values_[i];
        }
    }
    object_keys_.emplace_back(key);
    object_values_.push_back(std::move(value));
    return object_values_.back();
}

JsonValue& JsonValue::set_front(std::string_view key, JsonValue value)
{
    if (type_ != JsonType::Object)
    {
        type_ = JsonType::Object;
        object_keys_.clear();
        object_values_.clear();
    }
    for (std::size_t i = 0; i < object_keys_.size(); ++i)
    {
        if (object_keys_[i] == key)
        {
            object_values_[i] = std::move(value);
            return object_values_[i];
        }
    }
    object_keys_.insert(object_keys_.begin(), std::string(key));
    object_values_.insert(object_values_.begin(), std::move(value));
    return object_values_.front();
}

std::string JsonValue::dump(int indent) const
{
    std::string out;
    write(out, indent, 0);
    return out;
}

void JsonValue::write(std::string& out, int indent, int depth) const
{
    switch (type_)
    {
    case JsonType::Null: out += "null"; break;
    case JsonType::Bool: out += boolean_ ? "true" : "false"; break;
    case JsonType::Number: append_number(out, number_, number_is_float_); break;
    case JsonType::String: append_escaped(out, string_); break;
    case JsonType::Array:
    {
        if (array_.empty())
        {
            out += "[]";
            break;
        }
        out.push_back('[');
        for (std::size_t i = 0; i < array_.size(); ++i)
        {
            if (i > 0) out.push_back(',');
            if (indent > 0) append_indent(out, indent, depth + 1);
            array_[i].write(out, indent, depth + 1);
        }
        if (indent > 0) append_indent(out, indent, depth);
        out.push_back(']');
        break;
    }
    case JsonType::Object:
    {
        if (object_keys_.empty())
        {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (std::size_t i = 0; i < object_keys_.size(); ++i)
        {
            if (i > 0) out.push_back(',');
            if (indent > 0) append_indent(out, indent, depth + 1);
            append_escaped(out, object_keys_[i]);
            out.push_back(':');
            if (indent > 0) out.push_back(' ');
            object_values_[i].write(out, indent, depth + 1);
        }
        if (indent > 0) append_indent(out, indent, depth);
        out.push_back('}');
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Parsing and documents
// ---------------------------------------------------------------------------

std::optional<JsonValue> parse_json(std::string_view text, std::string* error)
{
    Parser parser{text};
    JsonValue value;
    if (!parser.parse_value(value))
    {
        if (error) *error = parser.error;
        return std::nullopt;
    }

    parser.skip_whitespace();
    if (parser.position != text.size())
    {
        parser.fail("unexpected trailing content");
        if (error) *error = parser.error;
        return std::nullopt;
    }

    if (error) error->clear();
    return value;
}

bool save_document(const std::string& path, JsonValue document, std::string* error)
{
    if (path.empty())
    {
        if (error) *error = "empty document path";
        return false;
    }

    if (!document.is_object()) document = JsonValue::object();
    document.set_front("version", kDocumentVersion);
    document.set_front("format", std::string(kDocumentFormat));

    std::string text = document.dump(2);
    text.push_back('\n');

    const std::string temporary = path + ".tmp";
    if (!fs::write_text(temporary, text))
    {
        if (error) *error = "cannot write " + temporary;
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(fs::to_path(path), ec);
    std::filesystem::rename(fs::to_path(temporary), fs::to_path(path), ec);
    if (ec)
    {
        std::error_code ignored;
        std::filesystem::remove(fs::to_path(temporary), ignored);
        if (error) *error = "cannot replace " + path + ": " + ec.message();
        return false;
    }

    if (error) error->clear();
    return true;
}

std::optional<JsonValue> load_document(const std::string& path, std::string* error)
{
    const auto text = fs::read_text(path);
    if (!text)
    {
        if (error) *error = "cannot read " + path;
        return std::nullopt;
    }

    std::string parse_error;
    auto document = parse_json(*text, &parse_error);
    if (!document)
    {
        if (error) *error = path + ": " + parse_error;
        return std::nullopt;
    }

    if (!document->is_object())
    {
        if (error) *error = path + ": the document root must be an object";
        return std::nullopt;
    }

    if (const JsonValue* format = document->find("format"))
    {
        if (format->as_string() != kDocumentFormat)
        {
            if (error) *error = path + ": unknown document format '" + format->as_string() + "'";
            return std::nullopt;
        }
    }

    if (const JsonValue* version = document->find("version"))
    {
        const int value = version->as_int(kDocumentVersion);
        if (value > kDocumentVersion)
        {
            if (error)
                *error = path + ": document version " + std::to_string(value) +
                         " is newer than this engine (" + std::to_string(kDocumentVersion) + ")";
            return std::nullopt;
        }
    }

    if (error) error->clear();
    return document;
}

// ---------------------------------------------------------------------------
// Typed conversions
// ---------------------------------------------------------------------------

JsonValue to_json(const Vec3& value)
{
    JsonValue out = JsonValue::array();
    out.push(value.x);
    out.push(value.y);
    out.push(value.z);
    return out;
}

JsonValue to_json(const Quat& value)
{
    JsonValue out = JsonValue::array();
    out.push(value.w);
    out.push(value.x);
    out.push(value.y);
    out.push(value.z);
    return out;
}

JsonValue to_json(const Color& value)
{
    JsonValue out = JsonValue::array();
    out.push(value.r);
    out.push(value.g);
    out.push(value.b);
    out.push(value.a);
    return out;
}

JsonValue to_json(const Transform& value)
{
    JsonValue out = JsonValue::object();
    out["position"] = to_json(value.position);
    out["rotation"] = to_json(value.rotation);
    out["scale"] = to_json(value.scale);
    return out;
}

bool from_json(const JsonValue& value, Vec3& out)
{
    if (value.is_array())
    {
        const auto& items = value.items();
        if (items.size() < 3) return false;
        out = Vec3(items[0].as_float(), items[1].as_float(), items[2].as_float());
        return true;
    }
    if (value.is_object())
    {
        out = Vec3(number_component(value, "x", 0.0f), number_component(value, "y", 0.0f),
                   number_component(value, "z", 0.0f));
        return true;
    }
    return false;
}

bool from_json(const JsonValue& value, Quat& out)
{
    if (value.is_array())
    {
        const auto& items = value.items();
        if (items.size() < 4) return false;
        out = Quat(items[0].as_float(), items[1].as_float(), items[2].as_float(),
                   items[3].as_float());
        return true;
    }
    if (value.is_object())
    {
        out = Quat(number_component(value, "w", 1.0f), number_component(value, "x", 0.0f),
                   number_component(value, "y", 0.0f), number_component(value, "z", 0.0f));
        return true;
    }
    return false;
}

bool from_json(const JsonValue& value, Color& out)
{
    if (value.is_array())
    {
        const auto& items = value.items();
        if (items.size() < 3) return false;
        out = Color(items[0].as_float(), items[1].as_float(), items[2].as_float(),
                    items.size() > 3 ? items[3].as_float(1.0f) : 1.0f);
        return true;
    }
    if (value.is_object())
    {
        out = Color(number_component(value, "r", 1.0f), number_component(value, "g", 1.0f),
                    number_component(value, "b", 1.0f), number_component(value, "a", 1.0f));
        return true;
    }
    return false;
}

bool from_json(const JsonValue& value, Transform& out)
{
    if (!value.is_object()) return false;
    out = Transform{};
    from_json(value["position"], out.position);
    from_json(value["rotation"], out.rotation);
    from_json(value["scale"], out.scale);
    return true;
}

Vec3 vec3_from_json(const JsonValue& value, const Vec3& fallback)
{
    Vec3 out = fallback;
    return from_json(value, out) ? out : fallback;
}

Quat quat_from_json(const JsonValue& value, const Quat& fallback)
{
    Quat out = fallback;
    return from_json(value, out) ? out : fallback;
}

Color color_from_json(const JsonValue& value, const Color& fallback)
{
    Color out = fallback;
    return from_json(value, out) ? out : fallback;
}

Transform transform_from_json(const JsonValue& value, const Transform& fallback)
{
    Transform out = fallback;
    return from_json(value, out) ? out : fallback;
}

} // namespace insland
