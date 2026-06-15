cmake_minimum_required(VERSION 3.27)
project(Neurus VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++ Standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# MSVC Settings
# ---------------------------------------------------------------------------
if(MSVC)
	add_compile_options(/W4 /utf-8 /MP)
	# Suppress MSVC-specific noisy warnings for third-party headers
	add_compile_options(/wd4100)  # unreferenced formal parameter
	add_compile_options(/wd4127)  # conditional expression is constant
	add_compile_options(/wd4189)  # local variable is initialized but not referenced
	add_compile_options(/wd4201)  # nonstandard extension: nameless struct/union
	add_compile_options(/wd4324)  # structure was padded due to alignment specifier
endif()

# ---------------------------------------------------------------------------
# Project Options
# ---------------------------------------------------------------------------
option(NEURUS_BUILD_TESTS "Build unit tests" ON)
option(NEURUS_ENABLE_VALIDATION "Enable Vulkan validation layers" OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# GLM (header-only math library)
add_subdirectory(dep/glm EXCLUDE_FROM_ALL)
target_include_directories(glm INTERFACE dep/glm)

# STB (header-only image library — stub, only stb_image.h needed later)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE dep/stb)

# Vulkan (from SDK)
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
	message(FATAL_ERROR "Vulkan SDK not found. Set VULKAN_SDK environment variable.")
endif()

# Qt6 (window, QML, EventBus)
find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml)

# Locate windeployqt for DLL deployment (Windows only)
if(WIN32)
	find_program(WINDEPLOYQT windeployqt
		HINTS "${Qt6_DIR}/../../../bin"  # e.g. D:/Qt/6.11.1/msvc2022_64/bin
		DOC "Path to windeployqt executable"
		REQUIRED
	)
	message(STATUS "Found windeployqt: ${WINDEPLOYQT}")
endif()

# ---------------------------------------------------------------------------
# Shader Compilation (GLSL → SPIR-V → C header)
# ---------------------------------------------------------------------------
include(cmake/CompileShaders.cmake)

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(NEURUS_BUILD_TESTS)
	enable_testing()
	add_subdirectory(dep/googletest EXCLUDE_FROM_ALL)
	add_subdirectory(test)
endif()
