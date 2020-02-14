# CMakeList.txt : CMake project for simulator, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
project("simulator")

# Add source to this project's executable.
INCLUDE_DIRECTORIES("C:\\Program\ Files\ (x86)\\Microsoft\ Visual\ Studio\\2019\\Community\\SDK\\ScopeCppSDK\\SDK\\include\\um")
LINK_DIRECTORIES("C:\\Program\ Files\ (x86)\\Microsoft\ Visual\ Studio\\2019\\Community\\SDK\\ScopeCppSDK\\SDK\\include\\um")
add_executable (simulator "main.c" "modbuscommands.c" "modbuscommands.h")
TARGET_LINK_LIBRARIES(${PROJECT_NAME} Ws2_32)

# TODO: Add tests and install targets if needed.
