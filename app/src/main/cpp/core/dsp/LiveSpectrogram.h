#pragma once

#include <cstdint>
#include <vector>

class Analyzer;

// Espectrograma en vivo (cascada) de lo que se está analizando: una columna cada 50 ms con lo
// último del analizador, en bandas logarítmicas de 20 Hz a 20 kHz y valores de 0 (silencio) a
// 255 (fuerte), con la misma escala que el de los archivos. Se usa desde un solo hilo.
class LiveSpectrogram {
public:
    static constexpr int kCols = 240, kRows = 128;  // 12 s
    static constexpr int kStepMs = 50;

    LiveSpectrogram();
    void reset();
    // Llegó un análisis nuevo: agrega las columnas que tocan hasta `nowMs`.
    void push(const Analyzer &a, int64_t nowMs);
    // Columnas agregadas desde el último reset (si cambia, hay que volver a dibujar).
    uint64_t count() const { return mCount; }
    // De la más vieja a la más nueva: [col * kRows + fila], fila 0 = graves. kCols × kRows.
    void read(uint8_t *out) const;

private:
    std::vector<uint8_t> mData;  // anillo de kCols columnas
    uint64_t mCount = 0;
    int64_t mNextMs = -1;
};
