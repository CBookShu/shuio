#include "shuio/shu_socket.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_server.h"
#include <cassert>
#include <thread>

using namespace std;

#define auto_make_test(exp) \
    if constexpr (std::is_same_v<bool, decltype(exp)>) {\
        assert(exp);\
    } else {\
        exp;\
    }

using namespace shu;

static void ssocket_test() {
    ssocket_opt opt{};
    ssocket s(opt);
    auto_make_test(s.init(0));
    auto_make_test(s.handle());
    auto_make_test(s.option());
    auto_make_test(s.reuse_addr(true));
    auto_make_test(s.reuse_port(true));
    auto_make_test(s.noblock(true));
    auto_make_test(s.nodelay(true));
}

static void sloop_test() {
    sloop_opt opt{};
    sloop loop{opt};
    
    sserver server({});
    struct server_ctx : sserver_runnable {
        virtual void new_client(socket_io_result_t res, sserver* svr, ssocket* s, addr_pair_t addr) noexcept override {
            if(res.err) {
                std::cout << "accept err:" << res.naviteerr << std::endl;
                return;
            }
            std::cout << "new client:" << addr.remote.ip << "," << addr.remote.port << std::endl;
            delete s;
        }
    };
    server.start(&loop, new server_ctx{}, {.iptype = 0, .port = 60000, .ip = {"0.0.0.0"} });
    loop.run();
}

int main(int argc, char**argv) {
    sloop_test();
    return 0;
}