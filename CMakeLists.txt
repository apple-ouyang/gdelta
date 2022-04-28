cmake_minimum_required(VERSION 3.10)
project(gdelta_original)

include_directories(.)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS}")


add_library(gdelta_original STATIC gdelta_original.cpp $<IF:$<C_COMPILER_ID:MSVC>,compat/getopt.c compat/msvc.c,>)
add_executable(gdelta_original.exe main.cpp gdelta_original.cpp $<IF:$<C_COMPILER_ID:MSVC>,compat/getopt.c compat/msvc.c,>)

target_compile_options(gdelta_original 
	PRIVATE
	  # MSVC
	  $<IF:$<C_COMPILER_ID:MSVC>,
            /Wall,
            -O3 -Wall -Werror -Wextra -mno-ms-bitfields
          >)

install(TARGETS gdelta_original gdelta_original.exe)
install(FILES gdelta_original.h DESTINATION include)
