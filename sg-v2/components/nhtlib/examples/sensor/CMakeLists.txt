cmake_minimum_required(VERSION 3.16)
# Make the nhtlib components (two levels up) visible to this project.
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../..)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(example_sensor)
