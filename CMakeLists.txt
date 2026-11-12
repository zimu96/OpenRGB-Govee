# OpenRGB-Govee: plugin OpenRGB per strisce LED Govee (H613A) via Bluetooth LE.
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)
project(GoveePlugin VERSION 1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus)

add_library(GoveePlugin MODULE
    src/GoveePlugin.cpp src/GoveeBle.cpp src/GoveeWidget.cpp)
target_include_directories(GoveePlugin PRIVATE src sdk)
target_link_libraries(GoveePlugin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus)
target_link_options(GoveePlugin PRIVATE -Wl,-z,defs)   # simboli mancanti = errore in compilazione, non al caricamento
set_target_properties(GoveePlugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
