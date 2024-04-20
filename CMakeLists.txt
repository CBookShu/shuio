cmake_minimum_required(VERSION 3.12.0)
project(shuio VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()

if(CMAKE_BUILD_TYPE MATCHES Debug)  
    message(STATUS "Debug mode is on")  
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -march=native")  
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Debug")
    set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}/Debug")
elseif(CMAKE_BUILD_TYPE MATCHES Release)  
    message(STATUS "Release mode is on")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -DCHECK_PTHREAD_RETURN_VALUE -D_FILE_OFFSET_BITS=64 -O2 -DNDEBUG -march=native")  
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")
    set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}/Release")
endif()

set(CMAKE_CXX_STANDARD 20)

set(shuio_files 
    shuio/shu_buffer.h 
    shuio/shu_buffer.cpp
    shuio/shu_client.h
    shuio/shu_common.h
    shuio/shu_loop.h
    shuio/shu_server.h
    shuio/shu_socket.h
    shuio/shu_stream.h
    )
set(lib_files shuio)
if(WIN32)  
    list(APPEND shuio_files
        shuio/win32/win32_detail.h
        shuio/win32/win32_detail.cpp
        shuio/win32/shu_client.cpp
        shuio/win32/shu_common.cpp
        shuio/win32/shu_loop.cpp
        shuio/win32/shu_server.cpp
        shuio/win32/shu_socket.cpp
        shuio/win32/shu_stream.cpp
    )
    add_compile_options(/wd4819)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")  
    list(APPEND shuio_files
        shuio/linux/linux_detail.h
        shuio/linux/linux_detail.cpp
        shuio/linux/shu_client.cpp
        shuio/linux/shu_common.cpp
        shuio/linux/shu_loop.cpp
        shuio/linux/shu_server.cpp
        shuio/linux/shu_socket.cpp
        shuio/linux/shu_stream.cpp
    )
    list(APPEND lib_files uring)
else()  
    error("Building for an unknown system")  
endif()

message(${shuio_files})

add_library(shuio STATIC ${shuio_files})
target_include_directories(shuio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(sio example/sio.cpp)
target_link_libraries(sio ${lib_files})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
