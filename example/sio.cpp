// sio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "shuio/shu_loop.h"
#include "shuio/shu_acceptor.h"
#include "shuio/shu_stream.h"
#include "shuio/shu_buffer.h"
#include "shuio/shu_client.h"
#include <queue>
#include <chrono>
#include <functional>
#include <charconv>
#include <optional>
#include <memory_resource>

using namespace shu;
using namespace std;

struct hash_addr_storage_t {
    std::size_t operator ()(const addr_storage_t& addr) const {
        std::string_view sip(addr.ip.data());
        return std::hash<const char*>()(sip.data()) ^ std::hash<int>()(addr.port);
    }
};

struct equal_addr_storage_t {
    bool operator()(const addr_storage_t& a, const addr_storage_t& b) const {
        return std::tie(a.ip, a.port) == std::tie(b.ip, b.port);
    }
};

// class tcp_client {
// public:
//     tcp_client(sloop& loop, addr_storage_t addr):loop_(loop) {
//         sclient client;
//         client.start(&loop_, addr, [this](socket_io_result_t res, std::unique_ptr<ssocket> sock, addr_pair_t addr) mutable {
//             if (res.err) {
//                 std::cout << "client connect error:" << res.naviteerr << std::endl;
//                 return;
//             }

//             sstream stream;
//             stream.start(&loop_, sock.release(), { .addr = addr }, [this](socket_io_result_t res, read_ctx_t& r) {
//                 on_read(res, r);
//             },
//                 [this](socket_io_result_t res, write_ctx_t& w) {
//                 on_write(res, w);
//             });
//             stream_.emplace(std::move(stream));
//             on_connect();
//         });
//     }

//     void write(socket_buffer buff) {
//         if (stream_) {
//             buff.commit();
//             stream_.value().write(std::move(buff));
//         }
//     }

//     void on_connect() {
//         socket_buffer buff("hello world");
//         write(std::move(buff));
//     }

//     void on_read(socket_io_result_t res, read_ctx_t& r) {
//         if (res.err) {
//             std::cout << "read err:" << res.naviteerr << endl;
//             stream_.reset();
//             return;
//         }
//         auto rd = r.buf.ready();
//         std::string_view str(rd.data(), rd.size());
//         std::cout << "client read:" << str << std::endl;
//         r.buf.commit(rd.size());
//     }

//     void on_write(socket_io_result_t res, write_ctx_t& w) {

//     }
// private:
//     sloop& loop_;
//     std::optional<sstream> stream_;
// };

void start_timer() {
    sloop l({});

    auto n = std::chrono::high_resolution_clock::now();
    decltype(n) n1;
    decltype(n) n2;
    l.add_timer([&]() {
        l.stop();
    }, 5s);
    l.add_timer([&]() {
        n1 = std::chrono::high_resolution_clock::now();
        l.add_timer([&]() {
            n2 = std::chrono::high_resolution_clock::now();
            l.stop();
        }, 2s);
    }, 1s);
    l.run();

    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(n1-n).count() << std::endl;
    
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(n2-n1).count() << std::endl;
}

int main(int argc, char**argv)
{
    start_timer();
    return 0;
}

