#include "utils/json_utils.hpp"
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace utils {

JsonValue JsonValue::null_value_;

const JsonValue& JsonValue::getProperty(const std::string& key) const {
    auto it = object_value_.find(key);
    return (it != object_value_.end()) ? it->second : null_value_;
}

JsonValue JsonParser::parse(const std::string& json) {
    size_t pos = 0;
    skipWhitespace(json, pos);
    return parseValue(json, pos);
}

std::string JsonParser::stringify(const JsonValue& value) {
    return stringifyValue(value);
}

JsonValue JsonParser::parseValue(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    
    if (pos >= json.length()) {
        throw std::runtime_error("Unexpected end of JSON");
    }
    
    char c = json[pos];
    
    if (c == '{') {
        return parseObject(json, pos);
    } else if (c == '[') {
        return parseArray(json, pos);
    } else if (c == '"') {
        return parseString(json, pos);
    } else if (c == 't' || c == 'f' || c == 'n') {
        return parseLiteral(json, pos);
    } else if (c == '-' || std::isdigit(c)) {
        return parseNumber(json, pos);
    } else {
        throw std::runtime_error("Unexpected character: " + std::string(1, c));
    }
}

JsonValue JsonParser::parseObject(const std::string& json, size_t& pos) {
    JsonValue obj;
    obj.setObject();
    
    pos++; // Skip '{'
    skipWhitespace(json, pos);
    
    if (pos < json.length() && json[pos] == '}') {
        pos++; // Skip '}'
        return obj;
    }
    
    while (pos < json.length()) {
        skipWhitespace(json, pos);
        
        // Parse key
        if (json[pos] != '"') {
            throw std::runtime_error("Expected string key in object");
        }
        
        JsonValue key = parseString(json, pos);
        skipWhitespace(json, pos);
        
        // Expect ':'
        if (pos >= json.length() || json[pos] != ':') {
            throw std::runtime_error("Expected ':' after object key");
        }
        pos++; // Skip ':'
        
        // Parse value
        JsonValue value = parseValue(json, pos);
        obj.setObjectProperty(key.asString(), value);
        
        skipWhitespace(json, pos);
        
        if (pos >= json.length()) {
            throw std::runtime_error("Unexpected end of JSON in object");
        }
        
        if (json[pos] == '}') {
            pos++; // Skip '}'
            break;
        } else if (json[pos] == ',') {
            pos++; // Skip ','
        } else {
            throw std::runtime_error("Expected ',' or '}' in object");
        }
    }
    
    return obj;
}

JsonValue JsonParser::parseArray(const std::string& json, size_t& pos) {
    JsonValue arr;
    arr.setArray();
    
    pos++; // Skip '['
    skipWhitespace(json, pos);
    
    if (pos < json.length() && json[pos] == ']') {
        pos++; // Skip ']'
        return arr;
    }
    
    while (pos < json.length()) {
        JsonValue value = parseValue(json, pos);
        arr.addArrayElement(value);
        
        skipWhitespace(json, pos);
        
        if (pos >= json.length()) {
            throw std::runtime_error("Unexpected end of JSON in array");
        }
        
        if (json[pos] == ']') {
            pos++; // Skip ']'
            break;
        } else if (json[pos] == ',') {
            pos++; // Skip ','
            skipWhitespace(json, pos);
        } else {
            throw std::runtime_error("Expected ',' or ']' in array");
        }
    }
    
    return arr;
}

JsonValue JsonParser::parseString(const std::string& json, size_t& pos) {
    pos++; // Skip opening '"'
    std::string result;
    
    while (pos < json.length()) {
        char c = json[pos];
        
        if (c == '"') {
            pos++; // Skip closing '"'
            return JsonValue(result);
        } else if (c == '\\') {
            pos++; // Skip '\'
            if (pos >= json.length()) {
                throw std::runtime_error("Unexpected end of JSON in string escape");
            }
            
            char escaped = json[pos];
            switch (escaped) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default:
                    throw std::runtime_error("Invalid escape sequence: \\" + std::string(1, escaped));
            }
        } else {
            result += c;
        }
        
        pos++;
    }
    
    throw std::runtime_error("Unterminated string");
}

JsonValue JsonParser::parseNumber(const std::string& json, size_t& pos) {
    size_t start = pos;
    
    if (json[pos] == '-') {
        pos++;
    }
    
    if (pos >= json.length() || !std::isdigit(json[pos])) {
        throw std::runtime_error("Invalid number format");
    }
    
    // Parse integer part
    if (json[pos] == '0') {
        pos++;
    } else {
        while (pos < json.length() && std::isdigit(json[pos])) {
            pos++;
        }
    }
    
    // Parse decimal part
    if (pos < json.length() && json[pos] == '.') {
        pos++;
        if (pos >= json.length() || !std::isdigit(json[pos])) {
            throw std::runtime_error("Invalid number format");
        }
        while (pos < json.length() && std::isdigit(json[pos])) {
            pos++;
        }
    }
    
    // Parse exponent part
    if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
        pos++;
        if (pos < json.length() && (json[pos] == '+' || json[pos] == '-')) {
            pos++;
        }
        if (pos >= json.length() || !std::isdigit(json[pos])) {
            throw std::runtime_error("Invalid number format");
        }
        while (pos < json.length() && std::isdigit(json[pos])) {
            pos++;
        }
    }
    
    std::string numberStr = json.substr(start, pos - start);
    double value = std::stod(numberStr);
    return JsonValue(value);
}

JsonValue JsonParser::parseLiteral(const std::string& json, size_t& pos) {
    if (json.substr(pos, 4) == "true") {
        pos += 4;
        return JsonValue(true);
    } else if (json.substr(pos, 5) == "false") {
        pos += 5;
        return JsonValue(false);
    } else if (json.substr(pos, 4) == "null") {
        pos += 4;
        return JsonValue();
    } else {
        throw std::runtime_error("Invalid literal");
    }
}

void JsonParser::skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.length() && std::isspace(json[pos])) {
        pos++;
    }
}

std::string JsonParser::stringifyValue(const JsonValue& value) {
    switch (value.getType()) {
        case JsonType::NULL_VALUE:
            return "null";
        case JsonType::BOOLEAN:
            return value.asBool() ? "true" : "false";
        case JsonType::NUMBER: {
            std::ostringstream oss;
            oss << value.asNumber();
            return oss.str();
        }
        case JsonType::STRING:
            return "\"" + escapeString(value.asString()) + "\"";
        case JsonType::ARRAY: {
            std::string result = "[";
            const auto& arr = value.asArray();
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) result += ",";
                result += stringifyValue(arr[i]);
            }
            result += "]";
            return result;
        }
        case JsonType::OBJECT: {
            std::string result = "{";
            const auto& obj = value.asObject();
            bool first = true;
            for (const auto& pair : obj) {
                if (!first) result += ",";
                result += "\"" + escapeString(pair.first) + "\":" + stringifyValue(pair.second);
                first = false;
            }
            result += "}";
            return result;
        }
    }
    return "null";
}

std::string JsonParser::escapeString(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

} // namespace utils