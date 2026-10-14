#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "sep/MelRoformer.h"

// Separa la voz de una canción completa como demix_track del original: trozos de chunk()
// muestras que se enciman (num_overlap por muestra), unidos con fundidos; con relleno por
// reflejo al principio y al final. La salida sale en orden y por partes (no hace falta tener
// la canción entera en memoria).
//
// Cada trozo calculado se puede guardar en `cacheDir`: si el trabajo se interrumpe (la app se
// cierra), al repetirlo con la misma entrada los trozos ya hechos no se vuelven a calcular.
class Separator {
public:
    // Da las muestras [from, from + n) de la mezcla (por canal), a la frecuencia del modelo.
    using Reader = std::function<void(int64_t from, int n, float *l, float *r)>;
    // Recibe la voz terminada, en orden. Devuelve false si no pudo (p. ej. disco lleno).
    using Writer = std::function<bool(const float *l, const float *r, int n)>;

    struct Options {
        int overlap = 0;        // 0 = el del modelo
        std::string cacheDir;   // "" = no guardar los trozos
        std::string cacheKey;   // identifica la entrada (si cambia, no se usan trozos viejos)
        const std::atomic<bool> *cancel = nullptr;
        std::function<void(int done, int total, int reused)> progress;  // trozos
    };

    static int chunkCount(int64_t length, int chunk, int overlap);
    // "" si salió bien; si no, el motivo ("Cancelado", sin espacio...).
    static std::string run(MelRoformer &model, ThreadPool &pool, int64_t length, const Reader &read,
                           const Writer &write, const Options &opt);
};
