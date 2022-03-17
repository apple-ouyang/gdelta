cmake_minimum_required(VERSION 3.10)
project(Gdelta)

include_directories(.)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -O3 -Wall -Werror -Wextra -mno-ms-bitfields")

add_library(gdelta STATIC gdelta.cpp)
add_executable(gdelta.exe main.cpp gdelta.cpp)

