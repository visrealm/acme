cmake_minimum_required(VERSION 3.22)

project(acme C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

add_subdirectory(src)

if (PROJECT_IS_TOP_LEVEL)
	include(CTest)
	add_subdirectory(testing)
endif()
