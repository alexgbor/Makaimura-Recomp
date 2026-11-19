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

# ---- The board (runtime/): the game's own code ----
add_library(gng_runtime STATIC runtime/src/board.c)
target_include_directories(gng_runtime PUBLIC runtime/include)

# ---- The reference: the original program on MAME's MC6809 core (m6809make.py writes the core's states from
# m6809.lst at build time, as MAME's build does) ----
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MAKAIMURA_M6809 ${MAKAIMURA_MAME}/m6809)
set(MAKAIMURA_GENERATED ${CMAKE_BINARY_DIR}/generated)
add_custom_command(OUTPUT ${MAKAIMURA_GENERATED}/cpu/m6809/m6809.hxx
	COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/reference/stdout_to.py
		${MAKAIMURA_GENERATED}/cpu/m6809/m6809.hxx ${MAKAIMURA_M6809}/m6809make.py ${MAKAIMURA_M6809}/m6809.lst
	DEPENDS ${MAKAIMURA_M6809}/m6809make.py ${MAKAIMURA_M6809}/m6809.lst ${MAKAIMURA_M6809}/base6x09.lst
	VERBATIM)
add_executable(gref tools/reference/gref.cpp ${MAKAIMURA_M6809}/m6809.cpp ${MAKAIMURA_M6809}/6x09dasm.cpp
	${MAKAIMURA_GENERATED}/cpu/m6809/m6809.hxx)
target_include_directories(gref PRIVATE tools/reference/stub ${MAKAIMURA_M6809} ${MAKAIMURA_GENERATED})
target_compile_features(gref PRIVATE cxx_std_20)
target_link_libraries(gref PRIVATE gng_runtime)
if(MSVC)
	target_compile_options(gref PRIVATE /wd4244 /wd4146 /wd4102)
endif()
