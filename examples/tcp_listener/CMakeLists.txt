cmake_minimum_required(VERSION 3.16)

# This library is the repository root (two folders up). Its component name is
# the folder name: "tcplib", or "esp32-tcplib" in a fresh clone from GitHub;
# main/CMakeLists.txt works it out.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tcp_listener)
