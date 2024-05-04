#include "linux_detail.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_loop.h"

namespace shu {
    struct global_init {
        global_init() {
            // 默认就给他处理掉吧，否则业务层还要判断平台再做操作。
            ::signal(SIGPIPE, SIG_IGN);
        }
        ~global_init() {
            
        }
    }G_wsa_initor;

    auto navite_cast_ssocket(ssocket* s) -> fd_navite_t* {
        return reinterpret_cast<fd_navite_t*>(s->handle());
    }
    auto navite_cast_sloop(sloop *loop) -> uring_navite_t *
    {
        return reinterpret_cast<uring_navite_t*>(loop->handle());
    }
}