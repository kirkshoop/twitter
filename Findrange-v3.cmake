# Copyright Gonzalo Brito Gadeschi 2015
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)
#
# Find the range-v3 include directory
# The following variables are set if range-v3 is found.
#  range-v3_FOUND        - True when the range-v3 include directory is found.
#  range-v3_INCLUDE_DIR  - The path to where the meta include files are.
# If range-v3 is not found, range-v3_FOUND is set to false.

# https://github.com/gnzlbg/ndtree/blob/master/cmake/Findrange-v3.cmake

find_package(PkgConfig)

if(NOT EXISTS "${range-v3_INCLUDE_DIR}")
  find_path(range-v3_INCLUDE_DIR
    NAMES range/v3/range.hpp 
    DOC "range-v3 library header files"
    )
endif()

if(EXISTS "${range-v3_INCLUDE_DIR}")
  include(FindPackageHandleStandardArgs)
  mark_as_advanced(range-v3_INCLUDE_DIR)
else()
  include(ExternalProject)
  ExternalProject_Add(range-v3
    GIT_REPOSITORY https://github.com/ericniebler/range-v3.git
    TIMEOUT 5
    CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    CONFIGURE_COMMAND "" # Disable configure step
    BUILD_COMMAND "" # Disable build step
    INSTALL_COMMAND "" # Disable install step
    UPDATE_COMMAND "" # Disable update step: clones the project only once
    )
  
  # Specify include dir
  ExternalProject_Get_Property(range-v3 source_dir)
  set(range-v3_INCLUDE_DIR ${source_dir}/include)
endif()

if(EXISTS "${range-v3_INCLUDE_DIR}")
  set(range-v3_FOUND 1)
else()
  set(range-v3_FOUND 0)
endif()
