#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// JSON pequeño para la sesión: leer y escribir. Los objetos guardan el orden de sus claves.
// Al leer, lo que falta o no es del tipo esperado devuelve el valor por omisión (como
// optX de org.json), así una sesión vieja o incompleta se abre igual.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : mType(Type::Bool), mBool(b) {}
    Json(int v) : Json(static_cast<double>(v)) {}
    Json(unsigned v) : Json(static_cast<double>(v)) {}
    Json(long v) : Json(static_cast<double>(v)) {}
    Json(unsigned long v) : Json(static_cast<double>(v)) {}
    Json(long long v) : Json(static_cast<double>(v)) {}
    Json(unsigned long long v) : Json(static_cast<double>(v)) {}
    Json(float v) : Json(static_cast<double>(v)) {}
    Json(double v) : mType(Type::Number), mNum(v) {}
    Json(const char *s) : mType(Type::String), mStr(s) {}
    Json(std::string s) : mType(Type::String), mStr(std::move(s)) {}

    static Json array();
    static Json object();

    Type type() const { return mType; }
    bool isNull() const { return mType == Type::Null; }
    bool isNumber() const { return mType == Type::Number; }
    bool isString() const { return mType == Type::String; }
    bool isArray() const { return mType == Type::Array; }
    bool isObject() const { return mType == Type::Object; }

    // ---------- Lectura
    bool asBool(bool def = false) const { return mType == Type::Bool ? mBool : def; }
    double asDouble(double def = 0) const { return mType == Type::Number ? mNum : def; }
    int64_t asInt(int64_t def = 0) const;
    std::string asString(const std::string &def = "") const { return mType == Type::String ? mStr : def; }
    size_t size() const;  // elementos de un arreglo o claves de un objeto
    const Json &operator[](size_t i) const;              // nulo si no existe
    const Json &operator[](int i) const { return (*this)[i < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(i)]; }
    const Json &operator[](const std::string &key) const;  // nulo si no existe
    const Json &operator[](const char *key) const { return (*this)[std::string(key)]; }
    bool has(const std::string &key) const;

    // ---------- Escritura
    Json &push(Json v);                          // agrega al arreglo
    Json &set(const std::string &key, Json v);   // pone (o reemplaza) en el objeto

    std::string dump() const;
    // Devuelve nulo y llena `error` si el texto no es JSON válido.
    static Json parse(const std::string &text, std::string &error);

private:
    void dumpTo(std::string &out) const;

    Type mType = Type::Null;
    bool mBool = false;
    double mNum = 0;
    std::string mStr;
    std::vector<Json> mArr;
    std::vector<std::pair<std::string, Json>> mObj;
};
