cmake_minimum_required(VERSION 3.7)
project(cubes CXX)

set(CMAKE_BUILD_TYPE "Release")

add_executable(${PROJECT_NAME} cubes.cpp)
target_link_libraries(${PROJECT_NAME} pthread)
# Enable C++17
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_compile_definitions(${PROJECT_NAME} PUBLIC NDEBUG)

target_compile_options(${PROJECT_NAME} PUBLIC
# Flags used for all build types:
	-Wall -Wextra
# Release build flags:
	$<$<CONFIG:Release>:-march=native>
	$<$<CONFIG:Release>:-O3>
# Optimized with debug info (good for profiling the code with perf)
	$<$<CONFIG:RelWithDebInfo>:-march=native>
	$<$<CONFIG:RelWithDebInfo>:-O3>
	$<$<CONFIG:RelWithDebInfo>:-gdwarf>
	$<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
)
