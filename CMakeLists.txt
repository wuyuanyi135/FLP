cmake_minimum_required(VERSION 3.16)
project(flp)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Werror)
add_executable(flp_test test.cpp)
