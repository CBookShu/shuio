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
#include <optional>

using namespace shu;
using namespace std;

struct hash_addr_storage_t {
    std::size_t operator ()(const addr_storage_t& addr) const {
        return std::hash<std::string>()(addr.ip) ^ std::hash<int>()(addr.port);
    }
};

struct equal_addr_storage_t {
    bool operator()(const addr_storage_t& a, const addr_storage_t& b) const {
        return std::tie(a.ip, a.port) == std::tie(b.ip, b.port);
    }
};

class tcp_client {
public:
    tcp_client(sloop& loop, addr_storage_t addr):loop_(loop) {
        sclient client;
        client.start(&loop_, addr, [this](socket_io_result_t res, std::unique_ptr<ssocket> sock, addr_pair_t addr) mutable {
            if (res.err) {
                std::cout << "client connect error:" << res.naviteerr << std::endl;
                return;
            }

            sstream stream;
            stream.start(&loop_, sock.release(), { .addr = addr }, [this](socket_io_result_t res, read_ctx_t& r) {
                on_read(res, r);
            },
                [this](socket_io_result_t res, write_ctx_t& w) {
                on_write(res, w);
            });
            stream_.emplace(std::move(stream));
            on_connect();
        });
    }

    void write(socket_buffer buff) {
        if (stream_) {
            buff.commit();
            stream_.value().write(std::move(buff));
        }
    }

    void on_connect() {
        socket_buffer buff("hello world");
        write(std::move(buff));
    }

    void on_read(socket_io_result_t res, read_ctx_t& r) {
        if (res.err) {
            std::cout << "read err:" << res.naviteerr << endl;
            stream_.reset();
            return;
        }
        auto rd = r.buf.ready();
        std::string_view str(rd.data(), rd.size());
        std::cout << "client read:" << str << std::endl;
        r.buf.commit(rd.size());
    }

    void on_write(socket_io_result_t res, write_ctx_t& w) {

    }
private:
    sloop& loop_;
    std::optional<sstream> stream_;
};

class tcp_server {
public:
    tcp_server(sloop& loop, addr_storage_t addr) :loop_(loop) {
        server_.start(&loop_, 
            [this](socket_io_result_t res,
                std::unique_ptr<ssocket> sock,
                addr_pair_t addr) {
            on_client(res, std::move(sock), addr);
        }, addr);
    }

    ~tcp_server() {
        server_.stop();
    }

    void on_client(socket_io_result_t res,
        std::unique_ptr<ssocket> sock,
        addr_pair_t addr) {
        if (res.err) {
            std::cout << "tcp server err:" << res.naviteerr << "," << strerror(res.naviteerr) << std::endl;
            return;
        }

        sstream stream;
        stream.start(&loop_, sock.release(), {.addr = addr}, 
            [addr, this](socket_io_result_t res, read_ctx_t& r) {
            on_read(addr, res, r);
        }, 
        [addr, this](socket_io_result_t res, write_ctx_t& w) {
            on_write(addr, res, w);
        });
        streams_.emplace( addr.remote, std::move(stream) );

        // server_.stop();
    }
    
    void on_read(const addr_pair_t& addr, socket_io_result_t res, read_ctx_t& r) {
        if (res.err) {
            // std::cout << "read err:" << res.naviteerr << endl;
            auto it = streams_.find(addr.remote);
            if (it != streams_.end()) {
                it->second.stop();
                streams_.erase(it);
            }
            // loop_.stop();
            return;
        }
        auto rd = r.buf.ready();
        std::string_view str(rd.data(), rd.size());
        // std::cout << "server read:" << str << std::endl;
        r.buf.consume(rd.size());

        auto it = streams_.find(addr.remote);
        if (it != streams_.end()) {
            socket_buffer buf(str);
            buf.commit();
            it->second.write(std::move(buf));
        }

        loop_.add_timer([this, addr](){
            auto it = streams_.find(addr.remote);
            if (it != streams_.end()) {
                it->second.stop();
            }
        }, 3s);
    }

    void on_write(const addr_pair_t& addr, socket_io_result_t res, write_ctx_t& w) {

    }
private:
    sloop& loop_;
    sserver server_;
    std::unordered_map<addr_storage_t, sstream, hash_addr_storage_t, equal_addr_storage_t> streams_;
};

void start_server() {
    sloop l({});
    tcp_server server(l, {.udp = false, .port = 8888});
    //tcp_client client(l, { .udp = false, .port = 5990, .ip = {"127.0.0.1"} });
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
    (void)r;
    addr_storage_t addr_server{ .udp = false, .port = port};
    svr.start(&l, acceptr_cb, addr_server);

    l.run();
}

int main(int argc, char**argv)
{
    start_server();
    // start_timer();
    // pingpong_server(argc, argv);
    return 0;
}

