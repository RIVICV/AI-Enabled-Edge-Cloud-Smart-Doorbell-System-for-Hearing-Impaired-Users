cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# MINIMAL_BUILD 已禁用，确保所有组件正确链接
# idf_build_set_property(MINIMAL_BUILD ON)
project(client)
