# src/data/CMakeLists.txt — Data & Resource Layer

add_library(neurus_data STATIC
	GPUResource.h
	GPUResource.cpp
)

target_link_libraries(neurus_data PUBLIC
	Vulkan::Vulkan
)

target_include_directories(neurus_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
