# Copyright Gonzalo Brito Gadeschi 2015
# Copyright Kirk Shoop 2017
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)
#
# Find the jsoncpp include directory
# The following variables are set if jsoncpp is found.
#  jsoncpp_FOUND        - True when the jsoncpp include directory is found.
#  jsoncpp_INCLUDE_DIR  - The path to where the meta include files are.
# If jsoncpp is not found, jsoncpp_FOUND is set to false.

# https://github.com/gnzlbg/ndtree/blob/master/cmake/

find_package(PkgConfig)

if(NOT EXISTS "${jsoncpp_INCLUDE_DIR}")
  find_path(jsoncpp_INCLUDE_DIR
    NAMES json.hpp 
    DOC "jsoncpp library header files"
    )
endif()

if(EXISTS "${jsoncpp_INCLUDE_DIR}/json.hpp")
  include(FindPackageHandleStandardArgs)
  mark_as_advanced(jsoncpp_INCLUDE_DIR)
else()
  include(ExternalProject)
  ExternalProject_Add(jsoncpp
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    TIMEOUT 5
    CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    CONFIGURE_COMMAND "" # Disable configure step
    BUILD_COMMAND "" # Disable build step
    INSTALL_COMMAND "" # Disable install step
    UPDATE_COMMAND "" # Disable update step: clones the project only once
    )
  
  # Specify include dir
  ExternalProject_Get_Property(jsoncpp source_dir)
  set(jsoncpp_INCLUDE_DIR ${source_dir}/src)
endif()

if(EXISTS "${jsoncpp_INCLUDE_DIR}")
  set(jsoncpp_FOUND 1)
else()
  set(jsoncpp_FOUND 0)
endif()
