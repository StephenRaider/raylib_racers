#pragma once
// A small JSON reader for the project's own data files (car specs, teams.json). Not a general
// validator: it accepts well-formed JSON and returns a null value on errors.
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mjson {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    const Value& operator[](const std::string& key) const {
        static const Value none;
        auto it = obj.find(key);
        return it == obj.end() ? none : it->second;
    }
    const Value& operator[](size_t i) const {
        static const Value none;
        return i < arr.size() ? arr[i] : none;
    }
    size_t size() const { return type == Array ? arr.size() : obj.size(); }
    double num(double def = 0) const { return type == Number ? n : def; }
    const std::string& str() const { return s; }
};

namespace detail {
inline void ws(const char*& p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
}
inline bool parse(const char*& p, Value& v, int depth);
inline bool parseString(const char*& p, std::string& out) {
    if (*p != '"') return false;
    ++p;
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'u': out += '?'; p += 4; break;  // not needed for our files
                case 0: return false;
                default: out += *p; break;
            }
            ++p;
        } else {
            out += *p++;
        }
    }
    if (*p != '"') return false;
    ++p;
    return true;
}
inline bool parse(const char*& p, Value& v, int depth) {
    if (depth > 64) return false;
    ws(p);
    if (*p == '{') {
        v.type = Value::Object;
        ++p;
        ws(p);
        if (*p == '}') { ++p; return true; }
        for (;;) {
            ws(p);
            std::string key;
            if (!parseString(p, key)) return false;
            ws(p);
            if (*p++ != ':') return false;
            if (!parse(p, v.obj[key], depth + 1)) return false;
            ws(p);
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return false;
        }
    }
    if (*p == '[') {
        v.type = Value::Array;
        ++p;
        ws(p);
        if (*p == ']') { ++p; return true; }
        for (;;) {
            v.arr.emplace_back();
            if (!parse(p, v.arr.back(), depth + 1)) return false;
            ws(p);
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return true; }
            return false;
        }
    }
    if (*p == '"') {
        v.type = Value::String;
        return parseString(p, v.s);
    }
    if (!std::strncmp(p, "true", 4)) { v.type = Value::Bool; v.b = true; p += 4; return true; }
    if (!std::strncmp(p, "false", 5)) { v.type = Value::Bool; p += 5; return true; }
    if (!std::strncmp(p, "null", 4)) { p += 4; return true; }
    char* end = nullptr;
    v.n = std::strtod(p, &end);
    if (end == p) return false;
    v.type = Value::Number;
    p = end;
    return true;
}
}  // namespace detail

inline Value parse(const std::string& text) {
    Value v;
    const char* p = text.c_str();
    if (!detail::parse(p, v, 0)) return Value{};
    return v;
}

}  // namespace mjson
