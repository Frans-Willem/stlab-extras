cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project(StlabExtras VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Werror")
set(CMAKE_CXX_STANDARD 14)

find_package(Threads REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.60.0 COMPONENTS system coroutine log unit_test_framework REQUIRED)

find_path(STLAB_INCLUDE_DIRS NAMES stlab/version.hpp PATHS ${CMAKE_SOURCE_DIR}/submodules/stlab-libraries/)
if(NOT STLAB_INCLUDE_DIRS)
	message(FATAL_ERROR "STLab libraries not found")
else()
	message(STATUS "STLab libraries found at: ${STLAB_INCLUDE_DIRS}")
endif()
add_library(stlab INTERFACE)
target_include_directories(stlab INTERFACE ${STLAB_INCLUDE_DIRS})

add_library(stlab-extras INTERFACE)
target_include_directories(stlab-extras INTERFACE ${CMAKE_SOURCE_DIR}/include/)
target_link_libraries(stlab-extras INTERFACE stlab)
target_link_libraries(stlab-extras INTERFACE Boost::system)
target_link_libraries(stlab-extras INTERFACE Threads::Threads)

add_subdirectory(examples)
