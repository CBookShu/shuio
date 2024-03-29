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
    3. 几乎所有的接口都是线程安全的（当然构建和析构除外）
    4. 接口的使用几乎无二义性
    5. 所有的类构建和析构都是安全的
        （server,client,stream等仅仅是个启动的壳子用来启动异步和停止，他们被析构也不妨碍异步操作继续执行）
## 进展
    1. UDP支持? TODO
    2. 多线程支持? TODO
    3. 是否需要封装业务代码(心跳、重连、消息包等)? TODO
