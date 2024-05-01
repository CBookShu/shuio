// sio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "shuio/shuio.h"
#include <queue>
#include <chrono>
#include <functional>
#include <charconv>
#include <optional>
#include <memory_resource>

using namespace shu;
using namespace std;

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


static char gMsg[] = "hello world";
class tcp_server {
public:
    tcp_server(sloop& loop, addr_storage_t addr) 
    :loop_(loop)
    {
        auto ok = server_.start(&loop_, {
            .evClose = [](sacceptor*){},
            .evConn = [this](socket_io_result_t res,
                ssocket* sock,
                addr_pair_t addr) {
            on_client(res, sock, addr);}
        }, addr);
        shu::panic(ok);
    }

    ~tcp_server() {
        server_.stop();
    }

    void on_client(socket_io_result_t res,
        ssocket* sock,
        addr_pair_t addr) {

        std::unique_ptr<ssocket> ptr(sock);
        if (res.res <= 0) {
            std::cout << "tcp server err:" << strerror(-res.res) << std::endl;
            server_.stop();
            return;
        }

        auto stream_ptr = std::make_unique<sstream>();

        stream_ptr->start(&loop_, ptr.release(), {.addr = addr}, {
            .evClose = [](sstream* s){
                delete s;
            }
        });
        auto p = stream_ptr.release();
        p->read([this, p](socket_io_result res, buffers_t bufs){
            on_read(p, res, bufs);
        },
        [this, p](int size, buffer_t& buf){
            // 可以是空的，不使用外部的buf
        });

        buffer_t buf;
        buf.p = gMsg;
        buf.size = strlen(gMsg);
        p->write(buf, [this, p](socket_io_result res){
            on_write(p, res);
        });
    }
    
    void on_read(sstream* s, socket_io_result_t res, buffers_t buf) {
        if (res.res <= 0) {
            // 3: 被对面关闭
            s->stop();
            return;
        }

        // 1: 读回调 stop
        // s->stop();
    }

    void on_write(sstream* s, socket_io_result_t res) {

        // 2: 写回调 stop
        // s->stop();
    }
private:
    sloop& loop_;
    sacceptor server_;
    std::pmr::unsynchronized_pool_resource pool_;
};

int main(int argc, char**argv)
{
    sloop loop;
    tcp_server server(loop, {8888});
    loop.run();
    return 0;
}

