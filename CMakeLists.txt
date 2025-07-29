cmake_minimum_required(VERSION 3.16)
project(convertrt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Set output directories for cleaner builds
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# For multi-config generators (like Visual Studio)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/${OUTPUTCONFIG})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/${OUTPUTCONFIG})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/${OUTPUTCONFIG})
endforeach()

find_package(Qt6 COMPONENTS Widgets Network REQUIRED)

add_executable(convertrt
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
)

target_link_libraries(convertrt
    PRIVATE Qt6::Widgets Qt6::Network
)

# Windows-specific settings
if(WIN32)
    # Set the executable to be a Windows application (not console)
    set_target_properties(convertrt PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
    # Add version info and icon if available
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources.rc")
        target_sources(convertrt PRIVATE resources.rc)
    endif()
endif()

# Copy config file to build directory
if(EXISTS "${CMAKE_SOURCE_DIR}/config.ini")
    configure_file("${CMAKE_SOURCE_DIR}/config.ini" "${CMAKE_BINARY_DIR}/config.ini" COPYONLY)
    # For multi-config generators
    foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
        configure_file("${CMAKE_SOURCE_DIR}/config.ini" "${CMAKE_BINARY_DIR}/${OUTPUTCONFIG}/config.ini" COPYONLY)
    endforeach()
endif()

# Install rules for packaging
install(TARGETS convertrt
    RUNTIME DESTINATION .
)

if(EXISTS "${CMAKE_SOURCE_DIR}/config.ini")
    install(FILES config.ini DESTINATION .)
endif()