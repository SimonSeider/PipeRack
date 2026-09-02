cmake_minimum_required(VERSION 3.20)
project(piperack VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(PULSE IMPORTED_TARGET libpulse)

set(SOURCES
    src/main.cpp
    src/AudioBackend.cpp
    src/MainWindow.cpp
    src/RackUnit.cpp
    src/WaveformView.cpp
    src/RackWidgets.cpp
)

if(PIPEWIRE_FOUND)
    list(APPEND SOURCES src/PipeWireBackend.cpp)
endif()

if(PULSE_FOUND)
    list(APPEND SOURCES src/PulseBackend.cpp)
endif()

add_executable(piperack ${SOURCES})

target_link_libraries(piperack PRIVATE Qt6::Widgets)

if(PIPEWIRE_FOUND)
    target_link_libraries(piperack PRIVATE PkgConfig::PIPEWIRE)
    target_compile_definitions(piperack PRIVATE HAVE_PIPEWIRE=1)
endif()

if(PULSE_FOUND)
    target_link_libraries(piperack PRIVATE PkgConfig::PULSE)
    target_compile_definitions(piperack PRIVATE HAVE_PULSE=1)
endif()

if(NOT PIPEWIRE_FOUND AND NOT PULSE_FOUND)
    message(FATAL_ERROR "Neither PulseAudio nor PipeWire was found. Please install one of them.")
endif()

message(STATUS "piperack backends: PipeWire=${PIPEWIRE_FOUND} PulseAudio=${PULSE_FOUND}")

target_compile_options(piperack PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

install(TARGETS piperack RUNTIME DESTINATION bin)
install(FILES packaging/piperack.desktop DESTINATION share/applications OPTIONAL)
