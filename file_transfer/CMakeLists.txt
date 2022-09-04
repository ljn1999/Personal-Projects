cmake_minimum_required(VERSION 3.13)
project(ece361 C)

set(CMAKE_C_STANDARD 99)

add_executable(ece361 server.c deliver.c)
