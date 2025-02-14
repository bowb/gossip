# This file (c) 2016-2020 AlertAvert.com.  All rights reserved.
cmake_minimum_required(VERSION 3.20)
project(gossip)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fPIC")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMAND ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_BINARY_DIR}
                      ${CMAKE_SOURCE_DIR}/CMake)

# Version number
set(RELEASE_MAJOR 1)
set(RELEASE_MINOR 0)
set(RELEASE_PATCH 5)

find_program(NAMES git REQUIRED)

execute_process(
  COMMAND git rev-parse --short HEAD
  RESULT_VARIABLE SHORT_HASH_RESULT
  OUTPUT_VARIABLE SHORT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(STATUS AND NOT STATUS EQUAL 0)
  set(BUILD_ID "")
else()
  string(STRIP BUILD_ID, BUILD_ID)
  set(BUILD_ID ${SHORT_HASH})
endif()

include(conan)

set(_CONAN_SETTING_COMPILER_CPPSTD ${CMAKE_CXX_STANDARD})

# Conan Packaging support only run conan_cmake_run if the conanfile.txt has
# changed or conanbuildinfo.cmake doesn't exists
set(CONAN_FILE ${CMAKE_SOURCE_DIR}/conanfile.txt)

set_property(
  DIRECTORY
  APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS ${CONANE_FILE})

option(CONAN_CMAKE_RUN "run conan cmake")

if(NOT EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
  set(CONAN_CMAKE_RUN ON)
endif()

file(SHA256 ${CONAN_FILE} CONAN_FILE_NEW_HASH)
set(CONAN_FILE_CACHED_HASH ${CMAKE_BINARY_DIR}/conanfile.hash)

if(EXISTS ${CONAN_FILE_CACHED_HASH})
  file(READ ${CONAN_FILE_CACHED_HASH} CONAN_FILE_OLD_HASH)
  if(NOT CONAN_FILE_NEW_HASH STREQUAL CONAN_FILE_OLD_HASH)
    file(WRITE ${CONAN_FILE_CACHED_HASH} ${CONAN_FILE_NEW_HASH})
    set(CONAN_CMAKE_RUN ON)
  endif()
else()
  file(WRITE ${CONAN_FILE_CACHED_HASH} ${CONAN_FILE_NEW_HASH})
endif()

if(CONAN_CMAKE_RUN)
  conan_cmake_run(
    CONANFILE
    ${CONAN_FILE}
    BUILD_TYPE
    "Release"
    BASIC_SETUP
    CMAKE_TARGETS
    NO_OUTPUT_DIRS
    BUILD
    missing
    SETTINGS
    compiler.cppstd=17
    UPDATE)
endif()

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
include(${CMAKE_BINARY_DIR}/conan_paths.cmake)

set(RELEASE_STRING
    "${RELEASE_MAJOR}.${RELEASE_MINOR}.${RELEASE_PATCH}-${BUILD_ID}")
message(STATUS "Building Release: ${RELEASE_STRING} (${CMAKE_BUILD_TYPE})")

set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(TESTS_DIR ${PROJECT_SOURCE_DIR}/tests)
set(PROTO_DIR ${PROJECT_SOURCE_DIR}/proto)
set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/proto)

# Configure a header file to pass settings into source code.
configure_file(${SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/gen/version.h)

find_package(Boost 1.84.0 REQUIRED COMPONENTS program_options system)
find_package(glog 0.7.0 REQUIRED)
find_package(ZeroMQ 4.3.5 REQUIRED)
find_package(cppzmq 4.10.0 REQUIRED)
find_package(date 3.0.1 REQUIRED)

# Building Protocol Buffers.
find_package(Protobuf 3.21.12 REQUIRED)

if(${PROTOBUF_FOUND})
  protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_DIR}/swim.proto)
  message(
    STATUS "Protocol Buffer headers: ${PROTO_HDRS}, Sources: ${PROTO_SRCS}")
  message(STATUS "Libraries in: ${PROTOBUF_LIBRARY}")
else()
  message(FATAL_ERROR "Could not find Google Protocol Buffers libraries")
endif()

include_directories(BEFORE ${INCLUDE_DIR} ${CMAKE_BINARY_DIR}/gen
                    ${PROTO_GEN_DIR} ${CMAKE_BINARY_DIR})

add_library(SWIM_UTILS-lib STATIC ${SOURCE_DIR}/utils/utils.cpp)
target_link_libraries(SWIM_UTILS-lib Boost::headers glog::glog date::date)

set(SWIM_SOURCES
    ${SOURCE_DIR}/swim/SwimClient.cpp ${SOURCE_DIR}/swim/SwimServer.cpp
    ${SOURCE_DIR}/swim/GossipFailureDetector.cpp)

set(SWIM_LIBS cppzmq protobuf::libprotobuf pthread libzmq-static SWIM_UTILS-lib)

set(SWIM_PUBLIC_HEADERS
    "${INCLUDE_DIR}/swim/GossipFailureDetector.hpp"
    "${INCLUDE_DIR}/swim/SwimClient.hpp" "${INCLUDE_DIR}/swim/SwimServer.hpp"
    "${INCLUDE_DIR}/swim/SwimCommon.hpp" "${PROTO_GEN_DIR}/swim.pb.h")
#
# libgossip.so
#
# Shared (dynamic) library for the SWIM Gossip Protocol.
#
add_library(gossip SHARED ${SWIM_SOURCES} ${PROTO_SRCS})
target_link_libraries(gossip PRIVATE ${SWIM_LIBS})

add_library(gossipStatic STATIC ${SWIM_SOURCES} ${PROTO_SRCS})
target_link_libraries(gossipStatic PRIVATE ${SWIM_LIBS})

set(API_SERVER_SOURCES ${SOURCE_DIR}/api/rest/ApiServer.cpp)
add_library(apiserver STATIC ${API_SERVER_SOURCES})
target_link_libraries(apiserver PRIVATE Boost::Boost)

# Unit tests build is defined in the tests/ folder
enable_testing()
add_subdirectory(${TESTS_DIR})

#
# Examples - demo execs/programs to show usage of the libraries/utilities.
#

set(EXAMPLES_DIR ${SOURCE_DIR}/examples)

# Examples will use the latest build for the shared libraries
link_directories(${CMAKE_BINARY_DIR}/lib ${INSTALL_DIR}/lib)

#
# SWIM Server
#
# Demo server to either listen to incoming pings, or send. Basic usage of the
# `SwimServer` class.
#
add_executable(swim_server_demo ${EXAMPLES_DIR}/server_demo.cpp)
target_link_libraries(
  swim_server_demo PRIVATE cppzmq libzmq-static glog::glog gossip
                           protobuf::libprotobuf Boost::program_options)

#
# Gossip Server
#
# Full implementation of the `GossipFailureDetector`.
#
add_executable(gossip_detector_example ${EXAMPLES_DIR}/gossip_example.cpp)
target_link_libraries(
  gossip_detector_example
  PRIVATE libzmq-static glog::glog gossip protobuf::libprotobuf apiserver
          Boost::program_options)

configure_file(${INCLUDE_DIR}/request/SimpleHttpRequest.hpp
               ${CMAKE_BINARY_DIR}/gen/)

install(FILES ${SWIM_PUBLIC_HEADERS} DESTINATION include)

install(
  TARGETS gossip
  EXPORT gossip-targets
  LIBRARY DESTINATION "lib${LIB_SUFFIX}" # .so file
  ARCHIVE DESTINATION "lib${LIB_SUFFIX}" # .lib file
)

install(
  TARGETS gossipStatic
  EXPORT gossip-targets
  LIBRARY DESTINATION "lib${LIB_SUFFIX}" # .so file
  ARCHIVE DESTINATION "lib${LIB_SUFFIX}" # .lib file
)
