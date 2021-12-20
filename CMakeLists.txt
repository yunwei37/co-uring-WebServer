cmake_minimum_required(VERSION 3.14)
project(co-uring-server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fcoroutines")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

set(CMAKE_SOURCE_DIR "server")

include_directories(${CMAKE_SOURCE_DIR}/include)

link_libraries(uring)

add_executable (co-uring-server
   "${CMAKE_SOURCE_DIR}/main.cpp" 
   )
