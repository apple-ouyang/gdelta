cmake_minimum_required(VERSION 3.10)
project(Gdelta)

include_directories(.)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS}")

if (CMAKE_BUILD_TYPE STREQUAL "Coverage")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
endif() 


add_library(gdelta STATIC gdelta.cpp $<IF:$<C_COMPILER_ID:MSVC>,compat/getopt.c compat/msvc.c,>)
add_executable(gdelta.exe main.cpp gdelta.cpp $<IF:$<C_COMPILER_ID:MSVC>,compat/getopt.c compat/msvc.c,>)

target_compile_options(gdelta 
	PRIVATE
	  # MSVC
	  $<IF:$<C_COMPILER_ID:MSVC>,
            /Wall,
            -O3 -Wall -Werror -Wextra -mno-ms-bitfields
          >)

install(TARGETS gdelta gdelta.exe)
install(FILES gdelta.h DESTINATION include)
