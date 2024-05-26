# shuio
    跨平台的异步io代码示例，支持win32(iocp)、linux(io_uring). 并提供了c++20 协程的结合示例(co_test)
    开发环境：
    windows: vs2022,c++20
    linux: wsl2(ubuntu 22.04.2),c++20

    shuio 定位的是更容易掌握的接口，用它可以比较稳定的组织IO 程序，并非是固定框架。shuio 的代码组织非常简单，跟原生裸写的性能差不多。
    此外,shuio 除了在linux上需要使用 liburing之外，没有任何依赖。非常容易部署、理解、修改。
    市面上大部分的IO 库或框架，都仅支持linux epoll，已经不新鲜了，于是这里使用uring，并且把win32 的iocp 也合并进来。
    最近c++20 的协程非常火，于是也手写了一份协程的示例（对于异步代码来说，协程连贯的上下文给buffer的维护提供了非常大的帮助)。

# 部署
- windows依赖

    安装visual studio 2022 版本的开发（支持c++20）

- linux依赖

    gcc >= 11.4.0 (支持c++20)
    编译部署uring: https://github.com/axboe/liburing.git

- 编译

    1. Release
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config=Release -j32
    2. Debug
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --config=Debug -j32

## 示例
    example中: 
    sio.cpp 是临时代码;
    pingpong 是一个pingpong 的测试用例
    bench_http_wrk 是一个测试wrk 命令的压力测试代码
    co_test 提供了一个简单的c++20 协程pingpong代码

## 设计思路
    所有基础类:
    支持：
        1. 默认构建
        2. 移动构建
    不支持:
        1. 复制
        2. 移动复制
    
    设计思路:
        1. 无法恢复的错误，均会触发panic 程序终止并打印调用信息
        2. io 的错误类型统一为 socket_io_result_t, >0 正常, <=0 错误
        3. 所有的类启动均为 start接口，停止stop（错误时需要外部主动调用stop）。stop 成功后，会在下一帧调用 close 回调，方便外部进行资源管理。
        4. 默认所有类型绑定在loop 上的时候，需要在该loop 上执行，不能跨线程；如果跨线程，请找到对应的loop 进行post或者dispach


## 支持功能
    1. tcp socket 异步读写
    2. 定时器
    3. win32 和 Linux 跨平台，接口一致，使用逻辑一致
## 进展
    1. UDP支持? TODO
    2. File支持? TODO
