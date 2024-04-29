#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"

namespace shu {
    AcceptExPtr win32_extension_fns::AcceptEx;
    ConnectExPtr win32_extension_fns::ConnectEx;
    GetAcceptExSockaddrsPtr win32_extension_fns::GetAcceptExSockaddrs;
    int win32_extension_fns::tcp_non_ifs_lsp_ipv4 = 1;
    int win32_extension_fns::tcp_non_ifs_lsp_ipv6 = 1;

    static void*
        get_extension_function(SOCKET s, const GUID* which_fn)
    {
        void* ptr = NULL;
        DWORD bytes = 0;
        WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
            (GUID*)which_fn, sizeof(*which_fn),
            &ptr, sizeof(ptr),
            &bytes, NULL, NULL);

        return ptr;
    }

    static void
        init_extension_functions()
    {
        const GUID acceptex = WSAID_ACCEPTEX;
        const GUID connectex = WSAID_CONNECTEX;
        const GUID getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET)
            return;
        win32_extension_fns::AcceptEx = (AcceptExPtr)get_extension_function(s, &acceptex);
        win32_extension_fns::ConnectEx = (ConnectExPtr)get_extension_function(s, &connectex);
        win32_extension_fns::GetAcceptExSockaddrs = (GetAcceptExSockaddrsPtr)get_extension_function(s,
            &getacceptexsockaddrs);

        WSAPROTOCOL_INFOW protocol_info;
        int opt_len = (int) sizeof protocol_info;
        if (getsockopt(s,
                SOL_SOCKET,
                SO_PROTOCOL_INFOW,
                (char*) &protocol_info,
                &opt_len) == 0) {
        if (protocol_info.dwServiceFlags1 & XP1_IFS_HANDLES)
            win32_extension_fns::tcp_non_ifs_lsp_ipv4 = 0;
        }
        closesocket(s);

        SOCKET dummy = socket(AF_INET6, SOCK_STREAM, IPPROTO_IP);
        if (dummy != INVALID_SOCKET) {
        opt_len = (int) sizeof protocol_info;
        if (getsockopt(dummy,
                        SOL_SOCKET,
                        SO_PROTOCOL_INFOW,
                        (char*) &protocol_info,
                        &opt_len) == 0) {
            if (protocol_info.dwServiceFlags1 & XP1_IFS_HANDLES)
                win32_extension_fns::tcp_non_ifs_lsp_ipv6 = 0;
            }
            closesocket(dummy);
        }
    }


    struct wsa_global_init {
        wsa_global_init() {
            WSADATA wsaData;
            (void)WSAStartup(MAKEWORD(2, 2), &wsaData);

            init_extension_functions();
        }
        ~wsa_global_init() {
            WSACleanup();
        }
    }G_wsa_initor;


    auto shu::navite_cast_loop(sloop* s) -> iocp_navite_t*
    {
        return reinterpret_cast<iocp_navite_t*>(s->handle());
    }

    auto shu::navite_cast_ssocket(ssocket* s) -> fd_navite_t*
    {
        return reinterpret_cast<fd_navite_t*>(s->handle());
    }
    auto navite_attach_iocp(sloop* l, ssocket* s) -> bool
    {
        auto* iocp = navite_cast_loop(l);
        auto* sock = navite_cast_ssocket(s);
        auto r = ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock->s), iocp->iocp, reinterpret_cast<ULONG_PTR>(&sock->tag), 0);
        return r;
    }
};
