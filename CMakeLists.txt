cmake_minimum_required(VERSION 3.16)
project(convertrt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

add_executable(convertrt
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
)

target_link_libraries(convertrt
    PRIVATE Qt6::Widgets
)