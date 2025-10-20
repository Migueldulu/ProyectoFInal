cmake_minimum_required(VERSION 3.22)
project(telemetria LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compile options
add_compile_options(
        -fno-exceptions
        -fno-rtti
)

# Android specific
if(ANDROID)
    add_definitions(-DANDROID)
endif()

# Sources
add_library(telemetria SHARED
        TelemetriaAPI.cpp
        GestorTelemetria.cpp
        AndroidUploader.cpp
        configReader.cpp
)

# Include directories
target_include_directories(telemetria PRIVATE .)

# FORZAR libc++ estático y eliminar -static-libstdc++
target_link_libraries(telemetria
        -lc++_static
        -lc++abi
        -landroid
        -llog
        -latomic
)

# Override del flag problemático - ELIMINA -static-libstdc++
target_link_options(telemetria PRIVATE
        "SHELL:-static-libstdc++"
        "SHELL:-Wl,-Bdynamic"
)

# Strip solo en Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(telemetria PRIVATE -s)
endif()