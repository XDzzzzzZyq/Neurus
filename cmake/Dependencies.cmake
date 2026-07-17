# ---------------------------------------------------------------------------
# Neurus Dependencies - Pre-compiled Binary Library Resolution
#
# Priority:
#   1. lib/<platform>/<dep>/  (pre-compiled, fast)
#   2. dep/<dep>/             (source build, fallback)
#   3. find_package()         (system SDK)
#
# Functions:
#   neurus_detect_platform()         - set NEURUS_PLATFORM (windows/linux)
#   neurus_find_dependency(name)     - resolve a dep from lib/ or dep/
# ---------------------------------------------------------------------------

function(neurus_detect_platform)
	if(WIN32)
		set(NEURUS_PLATFORM "windows" PARENT_SCOPE)
	elseif(APPLE)
		set(NEURUS_PLATFORM "macos" PARENT_SCOPE)
	elseif(UNIX)
		set(NEURUS_PLATFORM "linux" PARENT_SCOPE)
	else()
		message(FATAL_ERROR "Unsupported platform. Neurus currently supports Windows, Linux, and macOS.")
	endif()
endfunction()

# ---------------------------------------------------------------------------
# neurus_find_dependency(name)
#
# Resolves a dependency, preferring pre-compiled binaries in lib/<platform>/<name>/
# over source builds in dep/<name>/.
#
# For each dependency, the function:
#   1. Checks if lib/<platform>/<name>/lib/ contains the pre-compiled library
#   2. If found: creates an IMPORTED target pointing to the binary
#   3. If not found: falls back to add_subdirectory(dep/<name>)
#
# The expected lib/ layout for each dependency:
#   lib/<platform>/<name>/
#     lib/           - .lib / .dll / .so files
#     include/       - (optional) build-specific headers
#
# Headers are sourced from dep/<name>/ for API headers (which don't change
# between source and pre-compiled builds).
#
# Supported dependencies:
#   shaderc          - shared library (shaderc_shared)
#   qtadvanceddocking - static library (qtadvanceddocking-qt6)
# ---------------------------------------------------------------------------

function(neurus_find_dependency name)

	# --- Determine paths ---
	set(LIB_DIR "${CMAKE_SOURCE_DIR}/lib/${NEURUS_PLATFORM}/${name}")
	set(DEP_DIR "${CMAKE_SOURCE_DIR}/dep/${name}")

	# -----------------------------------------------------------------------
	# shaderc (shared library)
	# -----------------------------------------------------------------------
	if(name STREQUAL "shaderc")

		# shaderc headers are in the dep/ submodule
		set(SHADERC_INCLUDE_DIR "${DEP_DIR}/libshaderc/include")

		if(EXISTS "${LIB_DIR}/lib/shaderc_shared.lib")
			message(STATUS "Using pre-compiled shaderc from ${LIB_DIR}")

			# Create IMPORTED shared library target
			# DLL is config-independent — same binary for all build types
			add_library(shaderc_shared SHARED IMPORTED)
			set_target_properties(shaderc_shared PROPERTIES
				IMPORTED_LOCATION "${LIB_DIR}/lib/shaderc_shared.dll"
				IMPORTED_IMPLIB   "${LIB_DIR}/lib/shaderc_shared.lib"
			)

			# Provide include dirs so downstream targets can #include <shaderc/shaderc.h>
			set_property(TARGET shaderc_shared PROPERTY
				INTERFACE_INCLUDE_DIRECTORIES "${SHADERC_INCLUDE_DIR}"
			)

			# Mark that this dependency was resolved from lib/
			set(NEURUS_DEP_${name}_FROM_LIB TRUE PARENT_SCOPE)

		else()
			message(STATUS "Pre-compiled shaderc not found in ${LIB_DIR}, building from source")

			# Source build from dep/shaderc (existing behavior)
			set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
			set(SHADERC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
			set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)
			set(SHADERC_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
			set(ENABLE_OPT OFF CACHE BOOL "Enable spirv-opt capability" FORCE)
			add_subdirectory("${DEP_DIR}")

			# Include dirs are set by the source build's CMakeLists.txt
			set(NEURUS_DEP_${name}_FROM_LIB FALSE PARENT_SCOPE)
		endif()

	# -----------------------------------------------------------------------
	# qtadvanceddocking (static library)
	# -----------------------------------------------------------------------
	elseif(name STREQUAL "qtadvanceddocking")

		# ADS uses "_static" suffix for static builds and "d" debug postfix.
		# We need both variants because static libs embed CRT (/MD vs /MDd).
		set(ADS_LIB_RELEASE "${LIB_DIR}/lib/qtadvanceddocking-qt6_static.lib")
		set(ADS_LIB_DEBUG   "${LIB_DIR}/lib/qtadvanceddocking-qt6d_static.lib")

		# Check for at least the Release variant — Debug is optional (falls back to Release)
		if(EXISTS "${ADS_LIB_RELEASE}")
			message(STATUS "Using pre-compiled qtadvanceddocking from ${LIB_DIR}")

			# Create IMPORTED static library target
			add_library(qtadvanceddocking-qt6 STATIC IMPORTED)
			set_target_properties(qtadvanceddocking-qt6 PROPERTIES
				IMPORTED_LOCATION_RELEASE "${ADS_LIB_RELEASE}"
			)
			# Use Debug variant if available, otherwise fall back to Release
			if(EXISTS "${ADS_LIB_DEBUG}")
				set_target_properties(qtadvanceddocking-qt6 PROPERTIES
					IMPORTED_LOCATION_DEBUG "${ADS_LIB_DEBUG}"
				)
			else()
				set_target_properties(qtadvanceddocking-qt6 PROPERTIES
					IMPORTED_LOCATION_DEBUG "${ADS_LIB_RELEASE}"
				)
			endif()
			# Map remaining MSVC configs to Release/Debug
			set_target_properties(qtadvanceddocking-qt6 PROPERTIES
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
				MAP_IMPORTED_CONFIG_MINSIZEREL     Release
			)

			# ADS public headers
			set(ADS_INCLUDE_DIR "${DEP_DIR}/src")
			set_property(TARGET qtadvanceddocking-qt6 PROPERTY
				INTERFACE_INCLUDE_DIRECTORIES "${ADS_INCLUDE_DIR}"
			)

			# ADS requires Qt headers at compile time
			target_link_libraries(qtadvanceddocking-qt6 INTERFACE
				Qt6::Core Qt6::Gui Qt6::Widgets
			)

			# Create alias target to match add_subdirectory() convention
			add_library(ads::qtadvanceddocking-qt6 ALIAS qtadvanceddocking-qt6)

			set(NEURUS_DEP_${name}_FROM_LIB TRUE PARENT_SCOPE)

		else()
			message(STATUS "Pre-compiled qtadvanceddocking not found in ${LIB_DIR}, building from source")

			# Source build from dep/qtadvanceddocking (existing behavior)
			set(ADS_VERSION "4.5.0")
			set(BUILD_EXAMPLES OFF)
			set(BUILD_STATIC ON)
			add_subdirectory("${DEP_DIR}")

			set(NEURUS_DEP_${name}_FROM_LIB FALSE PARENT_SCOPE)
		endif()

	# -----------------------------------------------------------------------
	# Unknown dependency
	# -----------------------------------------------------------------------
	else()
		message(FATAL_ERROR "neurus_find_dependency: unknown dependency '${name}'. "
		                    "Supported: shaderc, qtadvanceddocking.")
	endif()

endfunction()
