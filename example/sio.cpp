// sio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <Windows.h>
#include <iostream>
#include "shuio/shu_loop.h"
#include "shuio/shu_server.h"
#include "shuio/shu_stream.h"
#include "shuio/shu_buffer.h"
#include <queue>
#include <chrono>
#include <format>
#include <functional>

using namespace shu;
using namespace std;

void start_server() {
    sloop l({});
    sserver svr({});

    struct stream_ctx : sstream_runable {
        virtual void on_read(socket_io_result_t res, sstream* s) noexcept override {
            if (res.err) {
                cout << "read err:" << res.naviteerr << endl;
                return;
            }

            auto buf = s->readbuffer();
            auto sp = buf->ready();
            auto str = std::string_view(sp.data(), sp.size());
            cout << str << endl;

            for (int i = 0; i < 10; ++i) {
                auto* wb = new socket_buffer(sp.size());
                wb->commit(sp.size());
                std::ranges::copy(sp, wb->ready().data());
                s->write(wb);
            }

            s->close();
        }
        virtual void on_write(socket_io_result_t res, sstream* s) noexcept override {
            cout << "on write" << endl;
        };
        virtual void on_close(const sstream* s) noexcept override {
            auto addr = s->option()->addr;
            cout << "close client:" << addr.remote.ip << "[" << addr.remote.port << "]" << endl;
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

    l.run();
}

void start_timer() {
    sloop l({});

    cout << shu::systime_t::now() << endl;
    l.add_timer_f([&l]() {
        cout << shu::systime_t::now() << endl;
        l.stop();
    }, 5s);
    l.add_timer_f([&l]() {
        l.stop();
    }, 2s);
    l.run();
}

int main()
{
    start_server();
    return 0;
}

