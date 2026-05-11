cmake_minimum_required(VERSION 3.20)
project(timer_engine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# 把src暴露为库，test和bench都能用
add_library(timer_lib STATIC
    src/timer_wheel.cpp
)
target_include_directories(timer_lib PUBLIC src)

add_subdirectory(test)
add_subdirectory(bench)