#include "io/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

const Json &nullJson() {
    static const Json n;
    return n;
}

void appendUtf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void dumpString(std::string &out, const std::string &s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);  // UTF-8 tal cual
                }
        }
    }
    out += '"';
}

class Parser {
public:
    explicit Parser(const std::string &t) : s(t) {}

    bool parseDocument(Json &out, std::string &error) {
        skipSpace();
        if (!value(out, 0)) {
            error = err.empty() ? "JSON inválido" : err;
            return false;
        }
        skipSpace();
        if (i != s.size()) {
            error = "Sobra texto después del JSON (posición " + std::to_string(i) + ")";
            return false;
        }
        return true;
    }

private:
    bool fail(const std::string &m) {
        if (err.empty()) err = m + " (posición " + std::to_string(i) + ")";
        return false;
    }

    void skipSpace() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }

    bool literal(const char *word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s.compare(i, n, word) != 0) return false;
        i += n;
        return true;
    }

    bool value(Json &out, int depth) {
        if (depth > 64) return fail("JSON demasiado anidado");
        if (i >= s.size()) return fail("Falta un valor");
        const char c = s[i];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            std::string str;
            if (!string(str)) return false;
            out = Json(std::move(str));
            return true;
        }
        if (c == 't') {
            if (!literal("true")) return fail("Valor desconocido");
            out = Json(true);
            return true;
        }
        if (c == 'f') {
            if (!literal("false")) return fail("Valor desconocido");
            out = Json(false);
            return true;
        }
        if (c == 'n') {
            if (!literal("null")) return fail("Valor desconocido");
            out = Json();
            return true;
        }
        return number(out);
    }

    bool number(Json &out) {
        const size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        if (i >= s.size() || !(s[i] >= '0' && s[i] <= '9')) return fail("Número inválido");
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                s[i] == '+' || s[i] == '-'))
            ++i;
        const std::string t = s.substr(start, i - start);
        char *end = nullptr;
        const double v = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0' || !std::isfinite(v)) return fail("Número inválido");
        out = Json(v);
        return true;
    }

    bool hex4(uint32_t &cp) {
        if (i + 4 > s.size()) return fail("Escape \\u incompleto");
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s[i++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
            else return fail("Escape \\u inválido");
        }
        return true;
    }

    bool string(std::string &out) {
        ++i;  // comilla
        while (true) {
            if (i >= s.size()) return fail("Texto sin cerrar");
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("Carácter de control en un texto");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) return fail("Escape incompleto");
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // par sustituto
                        uint32_t lo = 0;
                        if (i + 2 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            i += 2;
                            if (!hex4(lo)) return false;
                        }
                        if (lo < 0xDC00 || lo > 0xDFFF) return fail("Par sustituto inválido");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("Par sustituto inválido");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("Escape desconocido");
            }
        }
    }

    bool array(Json &out, int depth) {
        ++i;
        out = Json::array();
        skipSpace();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        while (true) {
            skipSpace();
            Json v;
            if (!value(v, depth + 1)) return false;
            out.push(std::move(v));
            skipSpace();
            if (i >= s.size()) return fail("Arreglo sin cerrar");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == ']') {
                ++i;
                return true;
            }
            return fail("Se esperaba , o ]");
        }
    }

    bool object(Json &out, int depth) {
        ++i;
        out = Json::object();
        skipSpace();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        while (true) {
            skipSpace();
            if (i >= s.size() || s[i] != '"') return fail("Se esperaba una clave");
            std::string key;
            if (!string(key)) return false;
            skipSpace();
            if (i >= s.size() || s[i] != ':') return fail("Se esperaba :");
            ++i;
            skipSpace();
            Json v;
            if (!value(v, depth + 1)) return false;
            out.set(key, std::move(v));
            skipSpace();
            if (i >= s.size()) return fail("Objeto sin cerrar");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == '}') {
                ++i;
                return true;
            }
            return fail("Se esperaba , o }");
        }
    }

    const std::string &s;
    size_t i = 0;
    std::string err;
};

}  // namespace

Json Json::array() {
    Json j;
    j.mType = Type::Array;
    return j;
}

Json Json::object() {
    Json j;
    j.mType = Type::Object;
    return j;
}

int64_t Json::asInt(int64_t def) const {
    if (mType != Type::Number) return def;
    if (!(mNum > -9.2e18 && mNum < 9.2e18)) return def;
    return static_cast<int64_t>(std::llround(mNum));
}

size_t Json::size() const {
    if (mType == Type::Array) return mArr.size();
    if (mType == Type::Object) return mObj.size();
    return 0;
}

const Json &Json::operator[](size_t i) const {
    return mType == Type::Array && i < mArr.size() ? mArr[i] : nullJson();
}

const Json &Json::operator[](const std::string &key) const {
    if (mType != Type::Object) return nullJson();
    for (const auto &kv : mObj)
        if (kv.first == key) return kv.second;
    return nullJson();
}

const std::string &Json::keyAt(size_t i) const {
    static const std::string empty;
    return mType == Type::Object && i < mObj.size() ? mObj[i].first : empty;
}

const Json &Json::valueAt(size_t i) const {
    return mType == Type::Object && i < mObj.size() ? mObj[i].second : nullJson();
}

bool Json::has(const std::string &key) const {
    if (mType != Type::Object) return false;
    for (const auto &kv : mObj)
        if (kv.first == key) return true;
    return false;
}

Json &Json::push(Json v) {
    if (mType != Type::Array) {
        *this = array();
    }
    mArr.push_back(std::move(v));
    return *this;
}

Json &Json::set(const std::string &key, Json v) {
    if (mType != Type::Object) {
        *this = object();
    }
    for (auto &kv : mObj) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return *this;
        }
    }
    mObj.emplace_back(key, std::move(v));
    return *this;
}

std::string Json::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

void Json::dumpTo(std::string &out) const {
    switch (mType) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += mBool ? "true" : "false"; break;
        case Type::Number: {
            char buf[40];
            if (!std::isfinite(mNum)) {
                out += "null";  // JSON no tiene infinitos
            } else if (mNum == std::floor(mNum) && std::fabs(mNum) < 9.0e15) {
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(mNum));
                out += buf;
            } else {
                std::snprintf(buf, sizeof buf, "%.15g", mNum);
                out += buf;
            }
            break;
        }
        case Type::String: dumpString(out, mStr); break;
        case Type::Array:
            out += '[';
            for (size_t k = 0; k < mArr.size(); ++k) {
                if (k) out += ',';
                mArr[k].dumpTo(out);
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t k = 0; k < mObj.size(); ++k) {
                if (k) out += ',';
                dumpString(out, mObj[k].first);
                out += ':';
                mObj[k].second.dumpTo(out);
            }
            out += '}';
            break;
    }
}

Json Json::parse(const std::string &text, std::string &error) {
    error.clear();
    Json out;
    Parser p(text);
    if (!p.parseDocument(out, error)) return Json();
    return out;
}
