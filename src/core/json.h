#pragma once

#include <map>
#include <string>
#include <vector>

namespace gtc {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    JsonValue() : type_(Type::Null), number_(0.0), boolean_(false) {}

    static JsonValue parse(const std::string& text, bool& ok);

    Type type() const { return type_; }
    double number(double fallback = 0.0) const;
    const std::string& string() const;
    const JsonValue* get(const std::string& key) const;
    std::string stringAt(const std::string& key, const std::string& fallback = "") const;
    double numberAt(const std::string& key, double fallback = 0.0) const;
    bool boolAt(const std::string& key, bool fallback = false) const;

private:
    friend class JsonParser;

    Type type_;
    double number_;
    bool boolean_;
    std::string text_;
    Array array_;
    Object object_;
};

std::string jsonEscape(const std::string& text);
std::string jsonNumber(float value);

} // namespace gtc
