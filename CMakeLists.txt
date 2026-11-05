cmake_minimum_required(VERSION 3.20)

project(acoustic_radar
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(PORTAUDIO REQUIRED
    IMPORTED_TARGET portaudio-2.0)

pkg_check_modules(FFTW3 REQUIRED
    IMPORTED_TARGET fftw3)

add_executable(acoustic_radar
    src/main.cpp
    src/audio.cpp
    src/chirp.cpp
    src/sync.cpp
    src/spectrum.cpp
    src/visualization.cpp
)

target_include_directories(acoustic_radar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(acoustic_radar PRIVATE
    SDL3::SDL3
    PkgConfig::PORTAUDIO
    PkgConfig::FFTW3
)

target_compile_options(acoustic_radar PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
