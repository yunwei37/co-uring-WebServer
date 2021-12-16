cmake_minimum_required(VERSION 3.16)
project(coroutines)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++2a -fcoroutines -pthread -luring")

set(SOURCE_FILES main.cpp)

SET(EXECUTABLE_OUTPUT_PATH "${LIBRARY_OUTPUT_PATH}")

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

#target_link_libraries(${PROJECT_NAME} rt dl pthread)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${EXECUTABLE_OUTPUT_PATH}"
)
