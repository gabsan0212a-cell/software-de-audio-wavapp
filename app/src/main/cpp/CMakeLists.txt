cmake_minimum_required(VERSION 3.22.1)
project(wavapp LANGUAGES CXX)

find_package(oboe REQUIRED CONFIG)

# core/: el corazón (todo lo técnico), sin nada de Android: se prueba también en Linux.
# android/: lo de la plataforma (salida de audio, decodificador del sistema, puente con Kotlin).
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS core/*.cpp)

add_library(wavapp SHARED
    ${CORE_SOURCES}
    android/AudioInput.cpp
    android/AudioOutput.cpp
    android/Fallas.cpp
    android/MediaDecoder.cpp
    android/jni_bridge.cpp)

target_include_directories(wavapp PRIVATE core android)
# Eigen (solo encabezados, MPL2): las multiplicaciones de matrices del separador de voces.
target_include_directories(wavapp SYSTEM PRIVATE third_party/eigen)
target_compile_definitions(wavapp PRIVATE _FILE_OFFSET_BITS=64)
target_compile_options(wavapp PRIVATE -Wall -Wextra)
target_link_libraries(wavapp oboe::oboe mediandk log)
