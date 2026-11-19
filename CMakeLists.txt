# Name:        CMakeLists.txt
# Description: Makaimura Recomp's build: for now the reference tools (MAME's 6809 core and disassembler)
# Version:     0.1.0
# Created:     19-11-2026
#
#   cmake -S . -B build/msvc                (Windows, Visual Studio 2022)
#   cmake --build build/msvc --config Release
#   cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release && cmake --build build/linux -j
cmake_minimum_required(VERSION 3.20)
project(makaimura VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32)
	set(MAKAIMURA_BIN_DEFAULT ${CMAKE_SOURCE_DIR}/build/bin)
else()
	string(TOLOWER "${CMAKE_SYSTEM_NAME}" MAKAIMURA_SYSTEM)
	set(MAKAIMURA_BIN_DEFAULT ${CMAKE_SOURCE_DIR}/build/bin-${MAKAIMURA_SYSTEM})
endif()
set(MAKAIMURA_BIN_DIR ${MAKAIMURA_BIN_DEFAULT} CACHE PATH "Where the executables go")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()
foreach(config IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${config} ${MAKAIMURA_BIN_DIR})
endforeach()

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	add_compile_options(/utf-8)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ---- The reference tools (tools/reference): MAME's code, never part of the game ----
set(MAKAIMURA_MAME ${CMAKE_SOURCE_DIR}/tools/reference/mame)

# MAME's 6809 disassembler as an oracle for tools/m6809/disassembler.py (tools/m6809/compare_with_mame.py)
add_executable(oracle_m6809 tools/reference/oracle_m6809.cpp ${MAKAIMURA_MAME}/m6809/6x09dasm.cpp)
target_include_directories(oracle_m6809 PRIVATE tools/reference/stub ${MAKAIMURA_MAME}/m6809)
target_compile_features(oracle_m6809 PRIVATE cxx_std_20)
