#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>

namespace utils {

// Simple JSON value types
enum class JsonType {
    NULL_VALUE,
    BOOLEAN,
    NUMBER,
    STRING,
    ARRAY,
    OBJECT
};

class JsonValue {
public:
    JsonValue() : type_(JsonType::NULL_VALUE) {}
    explicit JsonValue(bool value) : type_(JsonType::BOOLEAN), bool_value_(value) {}
    explicit JsonValue(double value) : type_(JsonType::NUMBER), number_value_(value) {}
    explicit JsonValue(const std::string& value) : type_(JsonType::STRING), string_value_(value) {}
    
    JsonType getType() const { return type_; }
    
    bool asBool() const { return bool_value_; }
    double asNumber() const { return number_value_; }
    const std::string& asString() const { return string_value_; }
    
    void setBool(bool value) { type_ = JsonType::BOOLEAN; bool_value_ = value; }
    void setNumber(double value) { type_ = JsonType::NUMBER; number_value_ = value; }
    void setString(const std::string& value) { type_ = JsonType::STRING; string_value_ = value; }
    
    // Array operations
    void setArray() { type_ = JsonType::ARRAY; array_value_.clear(); }
    void addArrayElement(const JsonValue& value) { array_value_.push_back(value); }
    const std::vector<JsonValue>& asArray() const { return array_value_; }
    
    // Object operations
    void setObject() { type_ = JsonType::OBJECT; object_value_.clear(); }
    void setObjectProperty(const std::string& key, const JsonValue& value) { object_value_[key] = value; }
    const std::map<std::string, JsonValue>& asObject() const { return object_value_; }
    bool hasProperty(const std::string& key) const { return object_value_.find(key) != object_value_.end(); }
    const JsonValue& getProperty(const std::string& key) const;
    
private:
    JsonType type_;
    bool bool_value_ = false;
    double number_value_ = 0.0;
    std::string string_value_;
    std::vector<JsonValue> array_value_;
    std::map<std::string, JsonValue> object_value_;
    static JsonValue null_value_;
};

class JsonParser {
public:
    static JsonValue parse(const std::string& json);
    static std::string stringify(const JsonValue& value);
    
private:
    static JsonValue parseValue(const std::string& json, size_t& pos);
    static JsonValue parseObject(const std::string& json, size_t& pos);
    static JsonValue parseArray(const std::string& json, size_t& pos);
    static JsonValue parseString(const std::string& json, size_t& pos);
    static JsonValue parseNumber(const std::string& json, size_t& pos);
    static JsonValue parseLiteral(const std::string& json, size_t& pos);
    
    static void skipWhitespace(const std::string& json, size_t& pos);
    static std::string stringifyValue(const JsonValue& value);
    static std::string escapeString(const std::string& str);
};

} // namespace utils