cmake_minimum_required(VERSION 3.25)
project(server VERSION 1.0)

set(CMAKE_C_COMPILER "/usr/bin/gcc")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -luring")
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
add_executable(server
                main.c
                io/io.c
                postbox/postbox.c
                util/list.c
)
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -fno-omit-frame-pointer -fsanitize=address")

# liburing 라이브러리 링크
find_library(URING_LIB uring REQUIRED)
target_link_libraries(server PRIVATE ${URING_LIB})
