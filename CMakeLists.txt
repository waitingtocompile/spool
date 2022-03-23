cmake_minimum_required (VERSION 3.14)

project ("spool")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory("src/spool")
add_subdirectory("test")
