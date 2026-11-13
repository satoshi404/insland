#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - lightweight JSON parser
// ---------------------------------------------------------------------------
// glTF itself is parsed by tinygltf v3.  This small parser is used for engine
// configuration, material side-cars and user metadata.  It intentionally has
// no external dependency and reports errors without throwing.
// ---------------------------------------------------------------------------

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace insland::json
{

class Value
{
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    enum class Type
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : data_(value) {}
    Value(double value) : data_(value) {}
    Value(int value) : data_(static_cast<double>(value)) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(const char* value) : data_(std::string(value ? value : "")) {}
    Value(Array value) : data_(std::move(value)) {}
    Value(Object value) : data_(std::move(value)) {}

    Type type() const
    {
        switch (data_.index())
        {
            case 0: return Type::Null;
            case 1: return Type::Boolean;
            case 2: return Type::Number;
            case 3: return Type::String;
            case 4: return Type::Array;
            default: return Type::Object;
        }
    }

    bool is_null() const { return type() == Type::Null; }
    bool is_bool() const { return type() == Type::Boolean; }
    bool is_number() const { return type() == Type::Number; }
    bool is_string() const { return type() == Type::String; }
    bool is_array() const { return type() == Type::Array; }
    bool is_object() const { return type() == Type::Object; }

    bool as_bool(bool fallback = false) const
    {
        return is_bool() ? std::get<bool>(data_) : fallback;
    }

    double as_number(double fallback = 0.0) const
    {
        return is_number() ? std::get<double>(data_) : fallback;
    }

    float as_float(float fallback = 0.0f) const
    {
        return static_cast<float>(as_number(static_cast<double>(fallback)));
    }

    int as_int(int fallback = 0) const
    {
        return static_cast<int>(as_number(static_cast<double>(fallback)));
    }

    const std::string& as_string() const
    {
        static const std::string empty;
        return is_string() ? std::get<std::string>(data_) : empty;
    }

    const Array& as_array() const
    {
        static const Array empty;
        return is_array() ? std::get<Array>(data_) : empty;
    }

    const Object& as_object() const
    {
        static const Object empty;
        return is_object() ? std::get<Object>(data_) : empty;
    }

    const Value* find(std::string_view key) const
    {
        if (!is_object()) return nullptr;
        const auto& object = std::get<Object>(data_);
        const auto it = object.find(std::string(key));
        return it == object.end() ? nullptr : &it->second;
    }

    const Value& operator[](std::string_view key) const
    {
        const Value* value = find(key);
        static const Value empty;
        return value ? *value : empty;
    }

    const Value& operator[](std::size_t index) const
    {
        const auto& array = as_array();
        static const Value empty;
        return index < array.size() ? array[index] : empty;
    }

    std::size_t size() const
    {
        if (is_array()) return std::get<Array>(data_).size();
        if (is_object()) return std::get<Object>(data_).size();
        if (is_string()) return std::get<std::string>(data_).size();
        return 0;
    }

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_{nullptr};
};

class Parser
{
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Value> parse(std::string* error = nullptr)
    {
        skip_whitespace();
        auto value = parse_value();
        if (!value)
        {
            if (error) *error = error_.empty() ? "invalid JSON" : error_;
            return std::nullopt;
        }
        skip_whitespace();
        if (pos_ != text_.size())
        {
            if (error) *error = "trailing characters after JSON value";
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<Value> parse_value()
    {
        if (pos_ >= text_.size())
        {
            error_ = "unexpected end of input";
            return std::nullopt;
        }

        switch (text_[pos_])
        {
            case 'n': return parse_literal("null", Value{});
            case 't': return parse_literal("true", Value{true});
            case 'f': return parse_literal("false", Value{false});
            case '"': {
                auto string = parse_string();
                if (!string) return std::nullopt;
                return Value(std::move(*string));
            }
            case '[': return parse_array();
            case '{': return parse_object();
            default:
                if (text_[pos_] == '-' || std::isdigit(static_cast<unsigned char>(text_[pos_])))
                    return parse_number();
                error_ = "unexpected character";
                return std::nullopt;
        }
    }

    std::optional<Value> parse_literal(std::string_view literal, Value value)
    {
        if (text_.substr(pos_, literal.size()) != literal)
        {
            error_ = "invalid literal";
            return std::nullopt;
        }
        pos_ += literal.size();
        return value;
    }

    std::optional<double> parse_number()
    {
        const char* begin = text_.data() + pos_;
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE)
        {
            error_ = "invalid number";
            return std::nullopt;
        }
        pos_ += static_cast<std::size_t>(end - begin);
        return value;
    }

    std::optional<std::string> parse_string()
    {
        if (pos_ >= text_.size() || text_[pos_] != '"')
        {
            error_ = "expected string";
            return std::nullopt;
        }
        ++pos_;

        std::string result;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_++];
            if (c == '"') return result;
            if (c != '\\')
            {
                result.push_back(c);
                continue;
            }

            if (pos_ >= text_.size()) break;
            const char esc = text_[pos_++];
            switch (esc)
            {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > text_.size())
                    {
                        error_ = "truncated unicode escape";
                        return std::nullopt;
                    }
                    unsigned codepoint = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        const char h = text_[pos_++];
                        codepoint <<= 4;
                        if (h >= '0' && h <= '9') codepoint |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') codepoint |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') codepoint |= static_cast<unsigned>(h - 'A' + 10);
                        else
                        {
                            error_ = "invalid unicode escape";
                            return std::nullopt;
                        }
                    }
                    append_utf8(result, codepoint);
                    break;
                }
                default:
                    error_ = "invalid escape sequence";
                    return std::nullopt;
            }
        }

        error_ = "unterminated string";
        return std::nullopt;
    }

    std::optional<Value> parse_array()
    {
        ++pos_; // '['
        Value::Array array;
        skip_whitespace();
        if (pos_ < text_.size() && text_[pos_] == ']')
        {
            ++pos_;
            return Value(std::move(array));
        }

        while (true)
        {
            skip_whitespace();
            auto value = parse_value();
            if (!value) return std::nullopt;
            array.push_back(std::move(*value));

            skip_whitespace();
            if (pos_ >= text_.size())
            {
                error_ = "unterminated array";
                return std::nullopt;
            }
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']')
            {
                ++pos_;
                return Value(std::move(array));
            }
            error_ = "expected ',' or ']'";
            return std::nullopt;
        }
    }

    std::optional<Value> parse_object()
    {
        ++pos_; // '{'
        Value::Object object;
        skip_whitespace();
        if (pos_ < text_.size() && text_[pos_] == '}')
        {
            ++pos_;
            return Value(std::move(object));
        }

        while (true)
        {
            skip_whitespace();
            auto key = parse_string();
            if (!key) return std::nullopt;

            skip_whitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':')
            {
                error_ = "expected ':'";
                return std::nullopt;
            }
            ++pos_;

            skip_whitespace();
            auto value = parse_value();
            if (!value) return std::nullopt;
            object.emplace(std::move(*key), std::move(*value));

            skip_whitespace();
            if (pos_ >= text_.size())
            {
                error_ = "unterminated object";
                return std::nullopt;
            }
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}')
            {
                ++pos_;
                return Value(std::move(object));
            }
            error_ = "expected ',' or '}'";
            return std::nullopt;
        }
    }

    void skip_whitespace()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    static void append_utf8(std::string& out, unsigned codepoint)
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

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;
};

inline std::optional<Value> parse(std::string_view text, std::string* error = nullptr)
{
    return Parser(text).parse(error);
}

inline std::optional<Value> parse_file(std::string_view path, std::string* error = nullptr)
{
    // Keep this header self-contained: the file loading helper lives in
    // core/filesystem.hpp, but including it here would create a cycle.
    std::string contents;
    {
        std::FILE* file = std::fopen(std::string(path).c_str(), "rb");
        if (!file)
        {
            if (error) *error = "could not open file";
            return std::nullopt;
        }
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (size > 0)
        {
            contents.resize(static_cast<std::size_t>(size));
            const std::size_t read = std::fread(contents.data(), 1, contents.size(), file);
            contents.resize(read);
        }
        std::fclose(file);
    }
    return parse(contents, error);
}

} // namespace insland::json
