#include "core/json.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace gtc {
namespace {
const std::string kEmptyString;
} // namespace

class JsonParser {
public:
    explicit JsonParser(const std::string& source) : source_(source) {}

    bool parse(JsonValue& output) {
        skipSpace();
        if (!parseValue(output)) {
            return false;
        }
        skipSpace();
        return position_ == source_.size();
    }

private:
    const std::string& source_;
    std::size_t position_ = 0;

    void skipSpace() {
        while (position_ < source_.size()) {
            const char value = source_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                break;
            }
            ++position_;
        }
    }

    bool consume(char expected) {
        skipSpace();
        if (position_ >= source_.size() || source_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    bool parseValue(JsonValue& output) {
        skipSpace();
        if (position_ >= source_.size()) {
            return false;
        }
        const char value = source_[position_];
        if (value == '{') {
            return parseObject(output);
        }
        if (value == '[') {
            return parseArray(output);
        }
        if (value == '"') {
            output.type_ = JsonValue::Type::String;
            return parseString(output.text_);
        }
        if (value == 't' && source_.compare(position_, 4, "true") == 0) {
            position_ += 4;
            output.type_ = JsonValue::Type::Bool;
            output.boolean_ = true;
            return true;
        }
        if (value == 'f' && source_.compare(position_, 5, "false") == 0) {
            position_ += 5;
            output.type_ = JsonValue::Type::Bool;
            output.boolean_ = false;
            return true;
        }
        if (value == 'n' && source_.compare(position_, 4, "null") == 0) {
            position_ += 4;
            output.type_ = JsonValue::Type::Null;
            return true;
        }
        return parseNumber(output);
    }

    bool parseString(std::string& output) {
        if (position_ >= source_.size() || source_[position_++] != '"') {
            return false;
        }
        output.clear();
        while (position_ < source_.size()) {
            const unsigned char value = static_cast<unsigned char>(source_[position_++]);
            if (value == '"') {
                return true;
            }
            if (value < 0x20U) {
                return false;
            }
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ >= source_.size()) {
                return false;
            }
            const char escaped = source_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > source_.size()) {
                    return false;
                }
                unsigned int codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = source_[position_++];
                    codepoint *= 16U;
                    if (digit >= '0' && digit <= '9') codepoint += static_cast<unsigned>(digit - '0');
                    else if (digit >= 'a' && digit <= 'f') codepoint += static_cast<unsigned>(digit - 'a' + 10);
                    else if (digit >= 'A' && digit <= 'F') codepoint += static_cast<unsigned>(digit - 'A' + 10);
                    else return false;
                }
                if (codepoint <= 0x7FU) {
                    output.push_back(static_cast<char>(codepoint));
                } else if (codepoint <= 0x7FFU) {
                    output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
                    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                } else {
                    output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
                    output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
                    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                }
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool parseNumber(JsonValue& output) {
        skipSpace();
        const std::size_t start = position_;
        if (position_ < source_.size() && source_[position_] == '-') ++position_;
        bool digits = false;
        while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
            ++position_;
            digits = true;
        }
        if (!digits) return false;
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') return false;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) ++position_;
            if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') return false;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        const std::string number = source_.substr(start, position_ - start);
        char* end = nullptr;
        output.number_ = std::strtod(number.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(output.number_)) return false;
        output.type_ = JsonValue::Type::Number;
        return true;
    }

    bool parseArray(JsonValue& output) {
        if (!consume('[')) return false;
        output.type_ = JsonValue::Type::Array;
        output.array_.clear();
        skipSpace();
        if (position_ < source_.size() && source_[position_] == ']') {
            ++position_;
            return true;
        }
        while (position_ < source_.size()) {
            JsonValue item;
            if (!parseValue(item)) return false;
            output.array_.push_back(std::move(item));
            skipSpace();
            if (position_ < source_.size() && source_[position_] == ']') {
                ++position_;
                return true;
            }
            if (!consume(',')) return false;
        }
        return false;
    }

    bool parseObject(JsonValue& output) {
        if (!consume('{')) return false;
        output.type_ = JsonValue::Type::Object;
        output.object_.clear();
        skipSpace();
        if (position_ < source_.size() && source_[position_] == '}') {
            ++position_;
            return true;
        }
        while (position_ < source_.size()) {
            skipSpace();
            std::string key;
            if (!parseString(key) || !consume(':')) return false;
            JsonValue item;
            if (!parseValue(item)) return false;
            output.object_[std::move(key)] = std::move(item);
            skipSpace();
            if (position_ < source_.size() && source_[position_] == '}') {
                ++position_;
                return true;
            }
            if (!consume(',')) return false;
        }
        return false;
    }
};

JsonValue JsonValue::parse(const std::string& text, bool& ok) {
    JsonValue result;
    JsonParser parser(text);
    ok = parser.parse(result);
    return result;
}

double JsonValue::number(double fallback) const {
    return type_ == Type::Number ? number_ : fallback;
}

const std::string& JsonValue::string() const {
    return type_ == Type::String ? text_ : kEmptyString;
}

const JsonValue* JsonValue::get(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    const auto found = object_.find(key);
    return found == object_.end() ? nullptr : &found->second;
}

std::string JsonValue::stringAt(const std::string& key, const std::string& fallback) const {
    const JsonValue* value = get(key);
    return value != nullptr && value->type_ == Type::String ? value->text_ : fallback;
}

double JsonValue::numberAt(const std::string& key, double fallback) const {
    const JsonValue* value = get(key);
    return value != nullptr && value->type_ == Type::Number ? value->number_ : fallback;
}

bool JsonValue::boolAt(const std::string& key, bool fallback) const {
    const JsonValue* value = get(key);
    return value != nullptr && value->type_ == Type::Bool ? value->boolean_ : fallback;
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream output;
    for (const unsigned char value : text) {
        switch (value) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (value < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(value) << std::dec;
            } else {
                output << static_cast<char>(value);
            }
        }
    }
    return output.str();
}

std::string jsonNumber(float value) {
    if (!std::isfinite(value)) return "0";
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << value;
    return output.str();
}

} // namespace gtc
