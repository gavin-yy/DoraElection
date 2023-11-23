cmake_minimum_required(VERSION 3.10)
project(dora_election)

# set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CPP_COMPILER g++)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if (NOT RELEASE)
	add_definitions("-g -O0")
endif()

set(SRCS discovery.cpp)

add_library(dora_election SHARED ${SRCS})
target_include_directories(dora_election PUBLIC thirdparty)


