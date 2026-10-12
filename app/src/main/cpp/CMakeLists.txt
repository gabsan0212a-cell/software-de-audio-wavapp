cmake_minimum_required(VERSION 3.22.1)
project(wavapp LANGUAGES CXX)

find_package(oboe REQUIRED CONFIG)

add_library(wavapp SHARED
    AudioEngine.cpp
    WavReader.cpp
    jni_bridge.cpp)

target_link_libraries(wavapp oboe::oboe log)
