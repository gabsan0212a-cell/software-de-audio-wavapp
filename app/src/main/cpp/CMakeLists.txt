cmake_minimum_required(VERSION 3.22.1)
project(wavapp LANGUAGES CXX)

find_package(oboe REQUIRED CONFIG)

add_library(wavapp SHARED
    AudioEngine.cpp
    Equalizer.cpp
    Importer.cpp
    Limiter.cpp
    MappedAudio.cpp
    Mixer.cpp
    Resampler.cpp
    WavReader.cpp
    WavWriter.cpp
    jni_bridge.cpp)

target_compile_definitions(wavapp PRIVATE _FILE_OFFSET_BITS=64)
target_compile_options(wavapp PRIVATE -Wall -Wextra)
target_link_libraries(wavapp oboe::oboe log)
