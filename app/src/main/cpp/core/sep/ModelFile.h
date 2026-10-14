#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "io/Json.h"

// Archivo de modelo de WavApp (.wvm), mapeado en memoria (no ocupa RAM de la app: el sistema
// trae del disco lo que se va leyendo). Lo escribe tools/modelos/formato.py:
//   "WAVAPPM1", largo de la cabecera (uint32), cabecera JSON, relleno a 64 y los tensores
//   (cada uno alineado a 64; "desplazamiento" cuenta desde el fin del relleno).
class ModelFile {
public:
    enum Type { kF32 = 0, kF16, kU8 };
    struct Tensor {
        Type type = kF32;
        std::vector<int64_t> shape;
        int64_t count = 0;  // valores
        const uint8_t *data = nullptr;
    };

    // nullptr y `error` si no se puede abrir o no es un modelo válido.
    static std::unique_ptr<ModelFile> open(const std::string &path, std::string &error);
    // El modelo dentro de otro archivo (un asset sin comprimir del APK): `length` bytes desde
    // `offset` en `fd`. No cierra `fd`.
    static std::unique_ptr<ModelFile> openFd(int fd, int64_t offset, int64_t length, std::string &error);
    ~ModelFile();
    ModelFile(const ModelFile &) = delete;
    ModelFile &operator=(const ModelFile &) = delete;

    const Json &header() const { return mHeader; }
    const Tensor *find(const std::string &name) const;
    // Copia a float32 (convierte float16 y u8). false si no está o no tiene `count` valores
    // (count < 0: los que tenga).
    bool floats(const std::string &name, std::vector<float> &out, int64_t count = -1) const;
    // Copia a float32 en `out`, que ya tiene lugar para todos sus valores.
    static void toFloat(const Tensor &t, float *out);

private:
    ModelFile() = default;
    bool parse(std::string &error);

    void *mMap = nullptr;
    size_t mMapBytes = 0;
    const uint8_t *mBase = nullptr;  // inicio del modelo dentro del mapeo
    size_t mBytes = 0;
    Json mHeader;
    std::map<std::string, Tensor> mTensors;
};
