cmake_minimum_required(VERSION 3.15.0)
project(example VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)  
set(CMAKE_CXX_STANDARD_REQUIRED ON)  

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/)  


# sio
add_executable(sio sio.cpp)
target_link_libraries(sio shuio)

# pingpong
add_executable(pingpong_client pingpong/client.cpp)
target_link_libraries(pingpong_client shuio)

add_executable(pingpong_server pingpong/server.cpp)
target_link_libraries(pingpong_server shuio)


#bench_http_wrk
#-- wrk -t4 -c1000 -d30s --latency http://127.0.0.1:8888
add_executable(bench_http_wrk bench_http_wrk/bench_http_wrk.cpp)
target_link_libraries(bench_http_wrk shuio)
