# shuio
    跨平台的异步io代码示例，支持win32(iocp)、linux(io_uring).
    开发环境：
    windows: vs2022,c++20
    linux: wsl2(ubuntu 22.04.2),c++20

# 部署
- windows依赖

    安装visual studio 2022 版本的开发（支持c++20）

- linux依赖

    gcc >= 11.4.0 (支持c++20)
    编译部署uring: https://github.com/axboe/liburing.git

- 编译

    1. xmake 编译
        
        安装 https://github.com/xmake-io/xmake.git
        
        cmd 或 PowerShell 中执行： xmake

        xmake 还有很多丰富的功能，可以自行了解。
    
    2.  cmake 编译
        
        cmake -B build -S .
        
        cmake --build build


## 示例
    example中: 
    sio.cpp 是临时代码;
    pingpong 是一个pingpong 的测试用例

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
