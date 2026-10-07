cmake_minimum_required(VERSION 3.28)
project(CMakeSFMLProject LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.1.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b # Latest supported by imgui-sfml
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(
    imgui-sfml
    GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
    GIT_TAG v3.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
)

set(SFML_BUILD_AUDIO ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SFML imgui)

# Evita que imgui-sfml llame find_package(SFML) — usará directamente
# el target SFML::Graphics que ya existe gracias al FetchContent de arriba.
# Esto evita por completo el problema de validación static/shared.
set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)

# imgui-sfml exige saber dónde está la carpeta raíz de ImGui (con imgui.h).
set(IMGUI_DIR ${imgui_SOURCE_DIR} CACHE PATH "" FORCE)

FetchContent_MakeAvailable(imgui-sfml)

add_executable(main src/main.cpp)
target_compile_features(main PRIVATE cxx_std_20)
target_compile_definitions(main PRIVATE SFML_STATIC)

target_link_libraries(main PRIVATE
    SFML::Graphics
    SFML::Audio
    ImGui-SFML::ImGui-SFML
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:main>/Assets"
    COMMENT "Copying Assets to build folder"
)