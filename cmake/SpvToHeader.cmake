# ---------------------------------------------------------------------------
# SpvToHeader.cmake — Convert SPIR-V binary to C uint32_t array header
#
# Usage: cmake -DSPV_FILE=shader.spv -DHEADER_FILE=shader.h -DARRAY_NAME=name -P SpvToHeader.cmake
# ---------------------------------------------------------------------------

file(READ "${SPV_FILE}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" SPV_LENGTH)

if(SPV_LENGTH EQUAL 0)
	message(FATAL_ERROR "SPIR-V file is empty or could not be read: ${SPV_FILE}")
endif()

math(EXPR SPV_SIZE "${SPV_LENGTH} / 8")

# Convert hex string to comma-separated uint32_t literals
set(COUNTER 0)
set(U32_LIST "")
while(COUNTER LESS SPV_SIZE)
	math(EXPR BYTE_OFFSET "${COUNTER} * 8")
	string(SUBSTRING "${SPV_HEX}" ${BYTE_OFFSET} 8 WORD_HEX)
	string(LENGTH "${WORD_HEX}" WORD_LEN)
	if(WORD_LEN LESS 8)
		message(FATAL_ERROR "SPIR-V hex data truncated at offset ${BYTE_OFFSET}: length=${WORD_LEN}, expected 8")
	endif()
	# Convert little-endian hex bytes to uint32_t value
	set(WORD_VALUE "0x")
	string(SUBSTRING "${WORD_HEX}" 6 2 B0)
	string(SUBSTRING "${WORD_HEX}" 4 2 B1)
	string(SUBSTRING "${WORD_HEX}" 2 2 B2)
	string(SUBSTRING "${WORD_HEX}" 0 2 B3)
	string(APPEND WORD_VALUE "${B0}${B1}${B2}${B3}")
	if(COUNTER GREATER 0)
		string(APPEND U32_LIST ",\n    ")
	endif()
	string(APPEND U32_LIST "${WORD_VALUE}")
	math(EXPR COUNTER "${COUNTER} + 1")
endwhile()

set(HEADER_GUARD "${ARRAY_NAME}_H")
string(TOUPPER "${HEADER_GUARD}" HEADER_GUARD)

set(HEADER_CONTENT
"// Auto-generated from SPIR-V binary. DO NOT EDIT.
#pragma once

#include <cstdint>

constexpr uint32_t ${ARRAY_NAME}[] = {
    ${U32_LIST}
};
constexpr size_t ${ARRAY_NAME}_size = sizeof(${ARRAY_NAME});
")

file(WRITE "${HEADER_FILE}" "${HEADER_CONTENT}")
message(STATUS "Generated shader header: ${HEADER_FILE} (${SPV_SIZE} words)")
