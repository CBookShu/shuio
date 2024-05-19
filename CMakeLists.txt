cmake_minimum_required(VERSION 3.15.0)
project(shuio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)  
set(CMAKE_CXX_STANDARD_REQUIRED ON)  

if(NOT DEFINED CMAKE_BUILD_TYPE)  
    # 如果没有设置 CMAKE_BUILD_TYPE，则默认设置为 Release  
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)  
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")  
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/)  


include(CTest)
enable_testing()

add_library(shuio STATIC "")
target_include_directories(shuio PUBLIC ${CMAKE_SOURCE_DIR})

if(WIN32) 
    target_compile_options(shuio PUBLIC /wd4819)
    target_sources(shuio PRIVATE
        shuio/shuio.h
        shuio/shu_stream.h
        shuio/shu_socket.h
        shuio/shu_loop.h
        shuio/shu_common.h
        shuio/shu_client.h
        shuio/shu_buffer.h
        shuio/shu_acceptor.h
        shuio/win32/shu_acceptor.cpp
        shuio/win32/shu_common.cpp
        shuio/win32/win32_detail.cpp
        shuio/win32/shu_socket.cpp
        shuio/win32/shu_client.cpp
        shuio/win32/shu_stream.cpp
        shuio/win32/shu_loop.cpp
        shuio/win32/shu_buffer.cpp
    )

    if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")  
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
    elseif(${CMAKE_BUILD_TYPE} STREQUAL "Release")  
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")  
    endif()
else()
    target_link_libraries(shuio PUBLIC uring)
    target_sources(shuio PRIVATE
        shuio/shuio.h
        shuio/shu_stream.h
        shuio/shu_socket.h
        shuio/shu_loop.h
        shuio/shu_common.h
        shuio/shu_client.h
        shuio/shu_buffer.h
        shuio/shu_acceptor.h
        shuio/linux/shu_acceptor.cpp
        shuio/linux/shu_common.cpp
        shuio/linux/linux_detail.cpp
        shuio/linux/shu_socket.cpp
        shuio/linux/shu_client.cpp
        shuio/linux/shu_stream.cpp
        shuio/linux/shu_loop.cpp
        shuio/linux/shu_buffer.cpp
    )

    if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")  
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
    elseif(${CMAKE_BUILD_TYPE} STREQUAL "Release")  
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -g")  
    endif()
endif(WIN32)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

add_subdirectory(example)