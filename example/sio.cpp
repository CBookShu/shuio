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

using namespace shu;
using namespace std;

void start_server() {
    sloop l({});
    sserver svr({});

    struct stream_ctx : sstream_runable {
        virtual void on_read(socket_io_result_t res, sstream::SPtr s) noexcept override {
            if (res.err) {
                cout << "read err:" << res.naviteerr << endl;
                return;
            }

            auto buf = s->readbuffer();
            auto sp = buf->ready();
            auto str = std::string_view(sp.data(), sp.size());
            cout << "svr " << str << endl;

            for (int i = 0; i < 10; ++i) {
                auto* wb = new socket_buffer(sp.size());
                wb->commit(sp.size());
                std::ranges::copy(sp, wb->ready().data());
                s->write(wb);
            }
            buf->consume(sp.size());
        }
        virtual void on_write(socket_io_result_t res, sstream::SPtr s) noexcept override {
            cout << "svr on write" << endl;
        };
        virtual void on_close(const sstream* s) noexcept override {
            auto addr = s->option()->addr;
            cout << "svr close client:" << addr.remote.ip << "[" << addr.remote.port << "]" << endl;
        };
    };

    struct server_ctx : sserver_runnable {
        virtual void new_client(socket_io_result_t res, sserver* svr, ssocket* sock, addr_pair_t addr) noexcept {
            if (res.err) {
                cout << "accept err:" << res.naviteerr << endl;
                return;
            }
            cout << "new client:" << addr.remote.ip << "[" << addr.remote.port << "]" << endl;
            sstream_opt opt = { .addr = addr };
            auto stream = std::make_shared<sstream>(svr->loop(), sock, opt);
            stream->start_read(new stream_ctx{});
        }
    };
    svr.start(&l, new server_ctx, { .iptype = 0, .port = 60000, .ip = {"0.0.0.0"} });

    struct stream_ctx_client : sstream_runable {
        virtual void on_read(socket_io_result_t res, sstream::SPtr s) noexcept override {
            if (res.err) {
                std::cout << "read err:" << res.naviteerr << endl;
                return;
            }

            auto buf = s->readbuffer();
            auto sp = buf->ready();
            auto str = std::string_view(sp.data(), sp.size());
            std::cout << "clent " << str << endl;

            buf->consume(sp.size());
        }
        virtual void on_write(socket_io_result_t res, sstream::SPtr s) noexcept override {
            std::cout << "clent on write" << endl;
        };
        virtual void on_close(const sstream* s) noexcept override {
            auto addr = s->option()->addr;
            std::cout << "clent close client:" << addr.remote.ip << "[" << addr.remote.port << "]" << endl;
        };
    } ;
    struct con_callback : connect_runable {
        sloop* loop;
        void run(socket_io_result res, ssocket* sock, addr_pair_t addr) noexcept override {
            if(res.err) return;

            sstream_opt opt = { .addr = addr };
            auto stream = std::make_shared<sstream>(loop, sock, opt);
            stream->start_read(new stream_ctx_client{});

            loop->add_timer_f([stream](){
                const char* s = "hello svr";
                auto* buf = new socket_buffer{strlen(s)};
                buf->commit(strlen(s));
                auto sp = buf->ready();
                std::memcpy(sp.data(), s, strlen(s));
                stream->write(buf);
            }, 2s);
            loop->add_timer_f([stream](){
                stream->close();
            }, 4s);
        }
        void destroy() noexcept {}
    }concb;
    concb.loop = &l;
    shu_connect(&l, {.iptype = 0, .port = 60000, .ip = {"127.0.0.1"}},&concb);
    l.add_timer_f([&l]() {
        l.stop();
    }, 2s);
    l.run();
}

void start_timer() {
    sloop l({});

    l.add_timer_f([&l]() {
        l.stop();
    }, 5s);
    l.add_timer_f([&l]() {
        l.add_timer_f([&l]() {
            l.stop();
        }, 2s);
    }, 1s);
    l.run();
}

int main()
{
    start_server();
    return 0;
}

