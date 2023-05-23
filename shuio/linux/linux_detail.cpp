#include "linux_detail.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_loop.h"

namespace shu {

    auto navite_cast_ssocket(ssocket* s) -> sock_navite_t* {
        return reinterpret_cast<sock_navite_t*>(s->handle());
    }
    auto navite_cast_sloop(sloop *loop) -> uring_navite_t *
    {
        return reinterpret_cast<uring_navite_t*>(loop->handle());
    }
}