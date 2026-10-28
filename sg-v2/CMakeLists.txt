cmake_minimum_required(VERSION 3.16.0)

# nhtlib is the reusable library bundle; its components live one level down.
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components/nhtlib)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sg-v2)
