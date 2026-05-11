# MAKERphone v2.4 firmware — ESP-IDF top-level build file
#
# Builds against ESP-IDF v5.5+ targeting esp32s3.
# Sibling to the legacy Arduino-based Chatter firmware at the repo root.
#
# Usage:
#   cd mp24
#   idf.py set-target esp32s3
#   idf.py build
#   idf.py -p <PORT> flash monitor

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(makerphone_mp24)
