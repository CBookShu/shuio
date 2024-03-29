// sio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "shuio/shu_loop.h"
#include "shuio/shu_server.h"
#include "shuio/shu_stream.h"
#include "shuio/shu_buffer.h"
#include "shuio/shu_client.h"
#include <queue>
#include <chrono>
#include <functional>
#include <charconv>

using namespace shu;
using namespace std;

void start_server() {
    sloop l({});
    sserver svr;

    sstream server_stream;
    auto server_stream_read = [&](socket_io_result_t res, read_ctx_t& r) {
        if (res.err) {
            std::cout << "read err:" << res.naviteerr << endl;
            return;
        }

        auto rd = r.buf.ready();
        std::string_view str(rd.data(), rd.size());
        std::cout << "server: " << str << endl;

        for (int i = 0; i < 10; ++i) {
            socket_buffer buf(rd.size());
            buf.prepare(rd).commit();
            server_stream.write(std::move(buf));
        }
        r.buf.consume(rd.size());
    };
    auto server_stream_write = [](socket_io_result_t res, write_ctx_t& w) {
        if (res.err) {
            std::cout << "write err:" << res.naviteerr << endl;
            return;
        }
        std::cout << "client write" << std::endl;
    };

    auto acceptr_cb = [&](socket_io_result_t res,
        std::unique_ptr<ssocket> sock, 
        addr_pair_t addr) {
        if (res.err) {
            std::cout << "accept err:" << res.naviteerr << std::endl;
            return;
        }

        std::cout << "new client:" 
            << addr.remote.ip 
            << "[" << addr.remote.port << "]" << endl;

        sstream_opt opt = { .addr = addr };
        server_stream.start(&l, sock.release(), opt, server_stream_read, server_stream_write);
    };


    addr_storage_t addr_server{ .udp = false, .port = 5990, .ip = {"0.0.0.0"} };
    svr.start(&l, acceptr_cb, addr_server);

    sstream client_stream;
    auto client_stream_read = [](socket_io_result_t res, read_ctx_t& r) {
        if (res.err) {
            std::cout << "client read error:" << res.naviteerr << std::endl;
            return;
        }

        auto rd = r.buf.ready();
        std::string_view str(rd.data(), rd.size());
        std::cout << "client: " << str << endl;
        r.buf.consume(rd.size());
    };
    auto client_stream_write = [](socket_io_result_t res, write_ctx_t& w) {
        
    };

    auto connect_cb = [&l,&client_stream, client_stream_read, client_stream_write](socket_io_result res,
        std::unique_ptr<ssocket> sock, 
        addr_pair_t addr) {
        
        if (res.err) {
            std::cout << "client connect err:" << res.naviteerr << std::endl;
            return;
        }
        sstream_opt opt = { .addr = addr };
        client_stream.start(&l, sock.release(), opt, client_stream_read, client_stream_write);

        l.add_timer([&client_stream]() {
            const char* s = "hello svr";
            socket_buffer buf(strlen(s));
            buf.prepare(s).commit();
            client_stream.write(std::move(buf));
            // client_stream.stop();
        }, 2s);
    };

    sclient client;
    addr_storage_t addr_conn{ .udp = false, .port = 5990, .ip = {"127.0.0.1"} };
    client.start(&l, addr_conn, connect_cb);
    l.run();
}

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

static void pingpong_server(int argc, char** argv) {
    // if(argc < 4) {
    //     fprintf(stderr, "Usage: server <address> <port> <threads>\n");
    //     return;
    // }

    /*
        由于shu_io 异步消息的属性，会大量合并读写，导致ping_pong的性能虚高。
        不过由于本身代码已经足够简单了，哪怕出现读写等待，理论上框架对于性能的影响
        也是微乎其微的，先这样吧
    */

    sloop l({});
    sserver svr;

    sstream server_stream;
    auto server_stream_read = [&](socket_io_result_t res, read_ctx_t& r) {
        if (res.err) {
            return;
        }
        auto rd = r.buf.ready();
        socket_buffer buf(rd.size());
        buf.prepare(rd).commit();
        server_stream.write(std::move(buf));
        r.buf.consume(rd.size());
    };
    auto server_stream_write = [](socket_io_result_t res, write_ctx_t& w) {

    };

    auto acceptr_cb = [&](socket_io_result_t res,
        std::unique_ptr<ssocket> sock, 
        addr_pair_t addr) {
        if (res.err) {
            std::cout << "accept err:" << res.naviteerr << std::endl;
            return;
        }

        // std::cout << "new client:" 
        //     << addr.remote.ip 
        //     << "[" << addr.remote.port << "]" << endl;

        sstream_opt opt = { .addr = addr };
        server_stream.start(&l, sock.release(), opt, server_stream_read, server_stream_write);
    };
    int port = 0;
    const char* sport = "9595";
    auto r = std::from_chars(sport, sport + strlen(sport), port);
    addr_storage_t addr_server{ .udp = false, .port = port};
    svr.start(&l, acceptr_cb, addr_server);

    l.run();
}

int main(int argc, char**argv)
{
    // start_server();
    // start_timer();
    pingpong_server(argc, argv);
    return 0;
}

