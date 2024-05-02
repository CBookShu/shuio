# shuio
    跨平台的异步io代码示例，支持win32(iocp)、linux(io_uring).
    开发环境：
    windows: vs2022,c++20
    linux: wsl2(ubuntu 22.04.2),c++20

# 部署
    安装xmake
    linux: 需要安装liburing

## 支持功能
    1. socket 异步读写
    2. 定时器
    3. buffer 非常容易管理
    4. 封装非常简洁，几乎跟裸写的 IO 性能相当【可以通过 example 中的 ping pong 测试验证】

## 说明
    1. win32 下 pingpong 的性能与 libuv+buffer池相当，甚至更好
    2. linux 下性能接近epoll
    3. pingpong 的测试用例抄了 muduo 的测试代码；整体的代码风格，更多的是借鉴libuv

## 进展
    1. UDP支持? TODO
    2. 多线程支持? TODO
    3. 是否需要封装业务代码(心跳、重连、消息包等)? TODO
