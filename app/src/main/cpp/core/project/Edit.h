#pragma once

#include <cstdint>
#include <vector>

#include "engine/Mixer.h"

// Operaciones de edición sobre un pedazo (Region). Son funciones puras: reciben un pedazo y
// devuelven el nuevo, siempre válido (largo mínimo, dentro del archivo, fundidos que caben).
namespace edit {

constexpr int64_t kMinLength = 2400;  // 50 ms: un pedazo no puede quedar más corto

Region move(const Region &r, int64_t start);
// Mueve el borde izquierdo a `newStart` (el final queda donde estaba).
Region trimStart(const Region &r, int64_t newStart);
// Mueve el borde derecho a `newEnd`, sin pasar del final del archivo de `fileFrames`.
Region trimEnd(const Region &r, int64_t newEnd, int64_t fileFrames);
// Fundidos: cada uno llega como máximo hasta donde empieza el otro.
Region setFadeIn(const Region &r, int64_t frames);
Region setFadeOut(const Region &r, int64_t frames);
// Corta en `at` (línea de tiempo). false si un lado quedaría demasiado corto.
bool cut(const Region &r, int64_t at, Region &left, Region &right);
// Imán: cuánto hay que correr los `edges` para que alguno caiga en uno de los `points`, si
// está a `threshold` o menos (el más cercano gana). 0 si no hay ninguno o threshold <= 0.
int64_t snapOffset(const std::vector<int64_t> &edges, const std::vector<int64_t> &points, int64_t threshold);

}  // namespace edit
