#pragma once

#include <string>

// Si el núcleo se cae (un acceso a memoria inválido, una división entera entre cero, una
// excepción que nadie atrapó...), deja anotado en `path` qué pasó y dónde (la pila, con la
// posición dentro de cada biblioteca) antes de que el sistema cierre la app como siempre. La
// pantalla lo ofrece la próxima vez para copiarlo. Con los símbolos de esa versión (los publica
// el CI) cada posición se traduce a función y línea.
void instalarFallas(const std::string &path);
