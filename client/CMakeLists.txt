cmake_minimum_required(VERSION 3.16)
project(echo_client C)

set(CMAKE_C_STANDARD 99)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(echo_client main.c)
target_link_libraries(echo_client Threads::Threads)
