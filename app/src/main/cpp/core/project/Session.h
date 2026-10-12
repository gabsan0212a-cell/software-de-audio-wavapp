#pragma once

#include "io/Json.h"
#include "project/Model.h"

// Sesión (sesion.json): la mezcla actual, para abrirla igual la próxima vez.
// Mismo formato que guardaba la versión anterior de la app, así una sesión vieja se abre igual.
Json sessionToJson(const ProjectModel &p);
// Llena `p` con lo que haya (lo que falte queda con su valor por omisión). Las pistas quedan
// sin espacio (slot -1) ni audio: el Studio las abre después. false si no es una sesión.
bool sessionFromJson(const Json &j, ProjectModel &p);
