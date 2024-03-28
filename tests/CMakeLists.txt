# This file (c) 2016-2017 AlertAvert.com.  All rights reserved.

project(gossip_test)
enable_testing()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fPIC")

# Conan Packaging support

find_package(GTest REQUIRED)
find_package(http_parser REQUIRED)
find_package(libuv REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(
  ${CMAKE_BINARY_DIR} ${GTest_INCLUDE_DIRS} ${http_parser_INCLUDE_DIRS}
  ${libuv_INCLUDE_DIRS} ${OpenSSL_INCLUDE_DIRS})

get_property(
  dirs
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "dir='${dir}'")
endforeach()

set(UNIT_TESTS ${TESTS_DIR}/test_gossip.cpp ${TESTS_DIR}/test_swim_server.cpp)

# Add the build directory to the library search path
link_directories(${CMAKE_BINARY_DIR})

set(INTEGRATION_TESTS ${TESTS_DIR}/test_integration.cpp)

# This is necessary as the .pb.cc and .pb.h files will not (yet) be present when
# we run cmake, and thus it will fail complaining it can't find the files. See
# http://stackoverflow.com/questions/19343018/cmake-and-findprotobuf as to why
# this is only necessary in this file (but not the parent).
#
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES GENERATED
                                                                   TRUE)

add_executable(unit_tests ${UNIT_TESTS} tests.h all_tests.cpp)
target_link_libraries(unit_tests Boost::Boost GTest::gtest
                      protobuf::libprotobuf gossip glog::glog)

add_executable(integration_tests ${INTEGRATION_TESTS} tests.h all_tests.cpp)
target_link_libraries(
  integration_tests
  Boost::Boost
  GTest::gtest
  glog::glog
  gossip
  apiserver
  uv
  OpenSSL::SSL
  OpenSSL::Crypto
  http_parser::http_parser
  protobuf::libprotobuf)

add_test(unit_tests ${CMAKE_CURRENT_BINARY_DIR}/unit_tests)
add_test(integration_tests ${CMAKE_CURRENT_BINARY_DIR}/integration_tests)
