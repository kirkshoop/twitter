# Copyright Gonzalo Brito Gadeschi 2015
# Copyright Kirk Shoop 2016
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)
#
# Find the rxcpp include directory
# The following variables are set if rxcpp is found.
#  rxcpp_FOUND        - True when the rxcpp include directory is found.
#  rxcpp_INCLUDE_DIR  - The path to where the meta include files are.
# If rxcpp is not found, rxcpp_FOUND is set to false.

# https://github.com/gnzlbg/ndtree/blob/master/cmake/Findrange-v3.cmake

find_package(PkgConfig)

if(NOT EXISTS "${rxcpp_INCLUDE_DIR}")
  find_path(rxcpp_INCLUDE_DIR
    NAMES rxcpp/rx.hpp 
    DOC "rxcpp library header files"
    )
endif()

if(EXISTS "${rxcpp_INCLUDE_DIR}")
  include(FindPackageHandleStandardArgs)
  mark_as_advanced(rxcpp_INCLUDE_DIR)
else()
  include(ExternalProject)
  ExternalProject_Add(rxcpp
    GIT_REPOSITORY https://github.com/Reactive-Extensions/RxCpp.git
    TIMEOUT 5
    CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    CONFIGURE_COMMAND "" # Disable configure step
    BUILD_COMMAND "" # Disable build step
    INSTALL_COMMAND "" # Disable install step
    UPDATE_COMMAND "" # Disable update step: clones the project only once
    )
  
  # Specify include dir
  ExternalProject_Get_Property(rxcpp source_dir)
  set(rxcpp_INCLUDE_DIR ${source_dir}/Rx/v2/src)
endif()

if(EXISTS "${rxcpp_INCLUDE_DIR}")
  set(rxcpp_FOUND 1)
else()
  set(rxcpp_FOUND 0)
endif()
