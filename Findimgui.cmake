# Copyright Gonzalo Brito Gadeschi 2015
# Copyright Kirk Shoop 2018
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)
#
# Find the imgui include directory
# The following variables are set if imgui is found.
#  imgui_FOUND        - True when the imgui include directory is found.
#  imgui_INCLUDE_DIR  - The path to where the meta include files are.
# If imgui is not found, imgui_FOUND is set to false.

# https://github.com/gnzlbg/ndtree/blob/master/cmake/

find_package(PkgConfig)

# if(NOT EXISTS "${imgui_INCLUDE_DIR}")
#   find_path(imgui_INCLUDE_DIR
#     NAMES imgui.h 
#     DOC "imgui library header files"
#     )
# endif()

# if(EXISTS "${imgui_INCLUDE_DIR}/imgui.h")
#   include(FindPackageHandleStandardArgs)
#   mark_as_advanced(imgui_INCLUDE_DIR)
# else()
  include(ExternalProject)
  ExternalProject_Add(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.69
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    LOG_DOWNLOAD TRUE
    LOG_UPDATE TRUE
    TIMEOUT 5
    # CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    CONFIGURE_COMMAND "" # Disable configure step
    BUILD_COMMAND "" # Disable build step
    INSTALL_COMMAND "" # Disable install step
    # UPDATE_COMMAND "" # Disable update step: clones the project only once
    )
  
  # Specify include dir
  ExternalProject_Get_Property(imgui source_dir)
  set(imgui_INCLUDE_DIR ${source_dir})
# endif()

if(EXISTS "${imgui_INCLUDE_DIR}")
  set(imgui_FOUND 1)
else()
  set(imgui_FOUND 0)
endif()
