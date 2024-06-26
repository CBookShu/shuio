﻿// sio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "shuio/shuio.h"
#include <queue>
#include <chrono>
#include <functional>
#include <charconv>
#include <optional>
#include <memory_resource>
#include <thread>

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
            .evConn = [this](sacceptor* a, socket_io_result_t res,
                UPtr<ssocket> sock,
                addr_pair_t addr) {
            on_client(res, std::move(sock), addr);}
        }, addr);
        shu::panic(ok);
    }

    ~tcp_server() {
        server_.stop();
    }

    void on_client(socket_io_result_t res,
        UPtr<ssocket> sock,
        addr_pair_t addr) {

        if (res.res <= 0) {
            std::cout << "tcp server err:" << strerror(-res.res) << std::endl;
            server_.stop();
            return;
        }

        auto stream_ptr = std::make_unique<sstream>();

        stream_ptr->start(&loop_, std::move(sock), {.addr = addr}, {
            .evClose = [](sstream* s){
                delete s;
            }
        });
        auto p = stream_ptr.release();
        p->read([this](sstream* s, socket_io_result res, buffers_t bufs){
            on_read(s, res, bufs);
        });

        buffer_t buf;
        buf.p = gMsg;
        buf.size = strlen(gMsg);
        p->write(buf, [this](sstream* s, socket_io_result res){
            on_write(s, res);
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
};

static void tcp_server_test() {
    sloop loop;
    tcp_server server(loop, {8888,"127.0.0.1"});
    loop.run();
}

class tcp_client {
    sloop* loop_;
    sclient client_;
public:
    tcp_client(sloop* loop, addr_storage_t addr) 
    : loop_(loop)
    {
        auto r = client_.start(loop_, addr, {
            .evClose = [](sclient *) {},
            .evConn = [this](socket_io_result res, UPtr<ssocket> sock, const addr_pair_t& addr) {
                on_client(res, std::move(sock), addr);
            }
        });
        shu::panic(r > 0);
    }

    void on_client(socket_io_result res, UPtr<ssocket> sock, const addr_pair_t& addr) {
        if (res.res <= 0) {
            client_.stop();
            return;
        }

        auto stream = std::make_unique<sstream>();
        stream->start(loop_, std::move(sock), {.addr = addr}, {
            .evClose = [](sstream* s){
                delete s;
            },
        });
        auto* s = stream.release();
        s->read([this](sstream* s, socket_io_result_t res, buffers_t bufs){
            on_read(s, res, bufs);
        });
    }

    void on_read(sstream* s, socket_io_result res, buffers_t bufs) {
        if (res.res <= 0) {
            s->stop();
            return;
        }

        std::string str;
        shu::copy_from_buffers(str, bufs);
        std::cout << str << std::endl;
    }
    void on_write(socket_io_result res) {

    }
};

static void tcp_client_test() {
    sloop loop;
    tcp_client server(&loop, {8888,"127.0.0.1"});
    loop.run();
}

static void loop_post() {
    std::cout << "loop_post start" << std::endl;
    sloop loop;
    int n = 0;
    int count = 10000000;
    std::thread t([&](){
        for (int i = 0; i < count; ++i) {
            loop.post([&](){
                n++;
            });
        }
        while(n == count) {
            std::this_thread::yield();
        }
        loop.stop();
    });
    loop.run();
    t.join();
    std::cout << "loop_post end" << std::endl;
}

template <typename F>
static void test_f(F&& f) {
    auto n1 = std::chrono::high_resolution_clock::now();
    std::forward<F>(f)();
    auto n2 = std::chrono::high_resolution_clock::now();
    std::cout << "cost " << std::chrono::duration_cast<std::chrono::milliseconds>(n2 - n1).count() << std::endl;
}

static void timer_test() {
    sloop l;
    test_f([&](){
        int value = 0;
        for (int i = 0; i < 1000; ++i) {
            auto id = l.add_timer([i, &value]() {
                std::cout << i << std::endl;
                value++;
            }, std::chrono::milliseconds(1) * i);

            if (i % 2 == 1) {
                l.cancel_timer(id);
            }
        }
    });
    l.run();
}

int main(int argc, char**argv)
{
    timer_test();
    return 0;
}

