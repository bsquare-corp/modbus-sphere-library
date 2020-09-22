#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

CMAKE_MINIMUM_REQUIRED(VERSION 3.11)
PROJECT(ModbusOnSphereM4 C)

# Set the tools revision
azsphere_configure_tools(TOOLS_REVISION "20.07")

# Create executable
ADD_EXECUTABLE(${PROJECT_NAME} main.c mt3620-intercore.c mt3620-uart.c mt3620-timer.c mt3620-gpio.c ../crc-util.c message-handler.c)
TARGET_LINK_LIBRARIES(${PROJECT_NAME})
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/linker.ld)

# Add MakeImage post-build command
INCLUDE("${AZURE_SPHERE_MAKE_IMAGE_FILE}")
