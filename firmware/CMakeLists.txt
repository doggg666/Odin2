cmake_minimum_required(VERSION 3.16)
# Include only integrated components; odin_wifi still needs C6 transport.
set(COMPONENTS main)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/board")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(odin_crowpanel_v1_0)
