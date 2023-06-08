#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"

namespace shu {
    auto shu::navite_cast_loop(sloop* s) -> iocp_navite_t*
    {
        return reinterpret_cast<iocp_navite_t*>(s->handle());
    }

    auto shu::navite_cast_ssocket(ssocket* s) -> sock_navite_t*
    {
        return reinterpret_cast<sock_navite_t*>(s->handle());
    }
    auto navite_attach_iocp(sloop* l, ssocket* s, IOCP_OP_TYPE type) -> bool
    {
        auto* iocp = navite_cast_loop(l);
        auto* sock = navite_cast_ssocket(s);
        return ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock->s), iocp->iocp, static_cast<ULONG_PTR>(type), 0);
    }
};
