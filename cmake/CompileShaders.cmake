# ---------------------------------------------------------------------------
# Shader Compilation: GLSL → SPIR-V → C Header
#
# Uses glslangValidator from the Vulkan SDK to compile shaders, then
# converts the binary SPIR-V output into a uint32_t C header array
# that can be #included directly.
# ---------------------------------------------------------------------------

# Find glslangValidator
find_program(GLSLANG_VALIDATOR glslangValidator
	HINTS "$ENV{VULKAN_SDK}/Bin"
	DOC "Path to glslangValidator executable"
)

if(NOT GLSLANG_VALIDATOR)
	message(WARNING "glslangValidator not found. Shaders will not be compiled. Set VULKAN_SDK environment variable.")
	return()
endif()

# Set shader source and output directories
set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/res/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Collect shader sources
file(GLOB SHADER_SOURCES "${SHADER_SOURCE_DIR}/*.vert" "${SHADER_SOURCE_DIR}/*.frag" "${SHADER_SOURCE_DIR}/*.comp")

# Create an interface target that other targets can link against
add_library(shader_headers INTERFACE)
target_include_directories(shader_headers INTERFACE ${SHADER_OUTPUT_DIR})

# Compile each shader
foreach(SHADER_SRC ${SHADER_SOURCES})
	get_filename_component(SHADER_NAME ${SHADER_SRC} NAME_WE)
	get_filename_component(SHADER_EXT ${SHADER_SRC} LAST_EXT)

	set(SPV_FILE "${SHADER_OUTPUT_DIR}/${SHADER_NAME}${SHADER_EXT}.spv")
	set(HEADER_FILE "${SHADER_OUTPUT_DIR}/${SHADER_NAME}${SHADER_EXT}.h")

	# Step 1: GLSL → SPIR-V
	add_custom_command(
		OUTPUT ${SPV_FILE}
		COMMAND ${GLSLANG_VALIDATOR} -V "${SHADER_SRC}" -o "${SPV_FILE}"
		DEPENDS ${SHADER_SRC}
		COMMENT "Compiling shader: ${SHADER_NAME}${SHADER_EXT}"
	)

	# Step 2: SPIR-V binary → C header (uint32_t array)
	add_custom_command(
		OUTPUT ${HEADER_FILE}
		COMMAND ${CMAKE_COMMAND}
			-DSPV_FILE=${SPV_FILE}
			-DHEADER_FILE=${HEADER_FILE}
			-DARRAY_NAME=${SHADER_NAME}_${SHADER_EXT}_spv
			-P ${CMAKE_SOURCE_DIR}/cmake/SpvToHeader.cmake
		DEPENDS ${SPV_FILE} ${CMAKE_SOURCE_DIR}/cmake/SpvToHeader.cmake
		COMMENT "Generating shader header: ${SHADER_NAME}${SHADER_EXT}.h"
	)

	# Add the header as a target-level dependency
	target_sources(shader_headers INTERFACE ${HEADER_FILE})

	# Ensure this shader header is generated before linking
	# Target name must be unique — include extension since vert/frag share the same stem
	string(SUBSTRING ${SHADER_EXT} 1 -1 SHADER_EXT_NO_DOT)
	add_custom_target(shader_${SHADER_NAME}_${SHADER_EXT_NO_DOT} DEPENDS ${HEADER_FILE})
	add_dependencies(shader_headers shader_${SHADER_NAME}_${SHADER_EXT_NO_DOT})
endforeach()
