#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// Automatización de volumen y paneo: una lista de puntos (cuadro, valor) por parámetro. Entre
// dos puntos el valor va en línea recta sobre la escala del carril (la que se dibuja), así lo
// que se ve es lo que suena. Antes del primer punto y después del último se queda su valor.
//
// Escala del carril ("posición", 0 abajo .. 1 arriba):
//   volumen: dB = 6 − 66·(1 − pos)²  (arriba +6 dB, abajo −60 dB = silencio, 0 dB en ~0.70)
//   paneo:   arriba izquierda (−100), centro 0.5, abajo derecha (+100)
namespace autom {

enum Param { kVolume = 0, kPan = 1, kParamCount = 2 };

constexpr int kMaxPoints = 4096;     // por carril
constexpr float kVolMin = -60.0f;    // = silencio
constexpr float kVolMax = 6.0f;

struct Point {
    int64_t frame = 0;
    float value = 0.0f;  // dB (volumen) o −100..100 (paneo)
    bool operator==(const Point &o) const { return frame == o.frame && value == o.value; }
    bool operator!=(const Point &o) const { return !(*this == o); }
};

// Los puntos de un carril. Copiarlo es barato (la lista se comparte, p. ej. con el historial
// para deshacer); cambiarlo es poner una lista nueva.
class Lane {
public:
    Lane() = default;
    Lane(std::vector<Point> pts) : mPts(std::make_shared<const std::vector<Point>>(std::move(pts))) {}  // NOLINT
    const std::vector<Point> &get() const { return mPts ? *mPts : none(); }
    bool empty() const { return get().empty(); }
    size_t size() const { return get().size(); }
    bool operator==(const Lane &o) const { return mPts == o.mPts || get() == o.get(); }
    bool operator!=(const Lane &o) const { return !(*this == o); }

private:
    static const std::vector<Point> &none() {
        static const std::vector<Point> e;
        return e;
    }
    std::shared_ptr<const std::vector<Point>> mPts;
};

// Punto ya en la escala del carril: lo que usa el mezclador.
struct PosPoint {
    int64_t frame = 0;
    float pos = 0.0f;
};

float toPos(int param, float value);
float fromPos(int param, float pos);
// Redondea al paso de cada parámetro (0.1 dB o 1 de paneo) y lo deja en su rango.
float clampValue(int param, float value);
// Ganancia lineal del volumen (0 en −60 dB) y paneo −1..1, desde la posición.
float gainFromPos(float pos);
float panFromPos(float pos);

// Valor en `frame` (interpolado en la escala del carril). `fallback` si no hay puntos.
float valueAt(int param, const std::vector<Point> &pts, int64_t frame, float fallback);
float posAt(const std::vector<PosPoint> &pts, int64_t frame);
std::vector<PosPoint> toPosPoints(int param, const std::vector<Point> &pts);

// Ordena, quita cuadros repetidos (gana el último) y negativos, y ajusta los valores.
void normalize(int param, std::vector<Point> &pts);
// Quita los puntos que sobran (los que quedan a menos de `tolerance` de la recta entre sus
// vecinos, medido en la escala del carril). El primero y el último se quedan.
std::vector<Point> simplify(int param, const std::vector<Point> &pts, float tolerance);

}  // namespace autom
