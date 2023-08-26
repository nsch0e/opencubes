cmake_minimum_required(VERSION 3.14)
project(cubes CXX)

# default to release build because speed maters.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "CMAKE_BUILD_TYPE: Release, Debug or RelWithDebInfo" FORCE)
endif()

if(NOT BUILD_CUBES_MAX_N)
	set(BUILD_CUBES_MAX_N 20 CACHE STRING "Limit of maximum N Polycubes to be computed")
endif()

if(NOT CUBES_PACK_CUBE_XYZ_ADDR)
	set(CUBES_PACK_CUBE_XYZ_ADDR ON CACHE BOOL "Pack Cube struct XYZ memory address into 56-bit field.")
endif()

# Try extract current HEAD commit-id in git
find_package(Git)
if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-list -n1 HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE RESULT
		OUTPUT_VARIABLE CONFIG_GIT_VERSION)
	message(STATUS "Set ${CONFIG_GIT_VERSION} to build version info")
endif()

# generate config.hpp header in build directory.
set(CONFIG_IS_READONLY "Warning: this file is overwritten during build. Do not edit.")
configure_file("config.hpp.in" "config.hpp")

include_directories("include")
include_directories("libraries")
include_directories("${PROJECT_BINARY_DIR}")

macro(ConfigureTarget Target)
	# Enable C++17
	target_compile_features(${Target} PUBLIC cxx_std_17)
	target_compile_definitions(${Target} PUBLIC
	# Debug defines:
		$<$<CONFIG:Debug>:DEBUG>
	# Release defines:
		$<$<CONFIG:Release>:NDEBUG>
		$<$<CONFIG:RelWithDebInfo>:NDEBUG>
	)
	target_compile_options(${Target} PUBLIC
	# Flags used for all build types:
		-Wall -Wextra
	# Debug build flags:
		$<$<CONFIG:Debug>:-O0>
		$<$<CONFIG:Debug>:-Werror>
		$<$<CONFIG:Debug>:-Wno-unknown-pragmas>
	# Release build flags:
		$<$<CONFIG:Release>:-O3>
		$<$<CONFIG:Release>:-march=native>
		$<$<CONFIG:Release>:-Wno-unknown-pragmas>
	# Optimized with debug info (good for profiling the code)
		$<$<CONFIG:RelWithDebInfo>:-march=native>
		$<$<CONFIG:RelWithDebInfo>:-O3>
		$<$<CONFIG:RelWithDebInfo>:-g>
		$<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
	)
endmacro()

add_library(mapped_file STATIC "libraries/mapped_file.cpp")
ConfigureTarget(mapped_file)

# Source files
add_library(CubeObjs OBJECT
	"src/cubes.cpp"
	"src/rotations.cpp"
	"src/newCache.cpp"
	"src/cubeSwapSet.cpp"
)
ConfigureTarget(CubeObjs)

# Build main program
add_executable(${PROJECT_NAME} "program.cpp" $<TARGET_OBJECTS:CubeObjs>)
target_link_libraries(${PROJECT_NAME} pthread)
target_link_libraries(${PROJECT_NAME} mapped_file)
ConfigureTarget(${PROJECT_NAME})

# Optionally build tests
option(BUILD_TESTS OFF "Build test suite")
if(BUILD_TESTS)
	add_subdirectory(tests)
endif()
