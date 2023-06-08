#include "shuio/shu_client.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "win32_detail.h"

#include <thread>
#include <future>
#include <cassert>

namespace shu {

    struct connect_complete_t : OVERLAPPED {
    };

    struct iocp_connect_op : iocp_sock_callback {
        sloop* loop;
        ssocket* sock;
        connect_runable* cb;
        addr_storage_t remote_addr;
        LPFN_CONNECTEX lpfnConnectEx;
        socket_user_op complete{};
        sockaddr_in addr{};

        ~iocp_connect_op() {
            if(sock) {
                delete sock;
            }
        }

        void init() {
            sock->init(0);
            complete.cb = this;

            GUID GuidConnectEx = WSAID_CONNECTEX;
            DWORD dwBytes;
            auto navite_sock = navite_cast_ssocket(sock);

            if (WSAIoctl(navite_sock->s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &GuidConnectEx, sizeof(GuidConnectEx),
                &lpfnConnectEx, sizeof(lpfnConnectEx),
                &dwBytes, NULL, NULL) == SOCKET_ERROR) {
                lpfnConnectEx = nullptr;
                return;
            }
            navite_attach_iocp(loop, sock, IOCP_OP_TYPE::SOCKET_TYPE);
        }

        void post_connect() {
            if (!lpfnConnectEx) {
                socket_io_result res{ .bytes = 0, .err = 1, .naviteerr = s_last_error()};
                cb->run(res, nullptr, {});
                cb->destroy();
                this->destroy();
                return;
            }

            auto* iocp = navite_cast_loop(loop);
            auto* s = navite_cast_ssocket(sock);
            struct sockaddr_in b_addr {};
            b_addr.sin_family = AF_INET;
            b_addr.sin_addr = in4addr_any;
            b_addr.sin_port = 0;

            if (::bind(s->s, (struct sockaddr*)&b_addr, sizeof(b_addr)) == SOCKET_ERROR) {
                socket_io_result res{ .bytes = 0, .err = 1, .naviteerr = s_last_error()};
                cb->run(res, nullptr, {});
                cb->destroy();
                this->destroy();    // 失败了就自裁！
                return;
            }

            int len = sizeof(addr);
            storage_2_sockaddr(&remote_addr, &addr);
            DWORD dwBytes = 0;
            auto ret = lpfnConnectEx(s->s, (struct sockaddr*)&addr, len, nullptr, 0, &dwBytes, &complete);
            if (!ret) {
                auto err = s_last_error();
                if (err != ERROR_IO_PENDING) {
                    socket_io_result res{ .bytes = 0, .err = 1, .naviteerr = err };
                    cb->run(res, nullptr, {});
                    cb->destroy();
                    this->destroy();    // 失败了就自裁！
                    return;
                }
            }
        }
        virtual void run(OVERLAPPED_ENTRY* entry) noexcept override {
            if(entry->dwNumberOfBytesTransferred != 0) {
                socket_io_result res{.bytes = 0, .err = 1, .naviteerr = s_last_error() };
                cb->run(res, nullptr, {});
                cb->destroy();
            } else {
                socket_io_result res{.err = 0};
                auto* tmp =std::exchange(sock, nullptr);
                addr_storage_t local_addr;
                sockaddr_2_storage(&addr, &local_addr);
                addr_pair_t addr_pair{.remote = remote_addr, .local = local_addr};
                cb->run(res, tmp, addr_pair);
                cb->destroy();
            }
            destroy();  // 你的任务完成了！
        }
    };

    void shu_connect(sloop* loop, 
    addr_storage_t saddr, 
    connect_runable* cb) 
    {
        loop->dispatch_f([loop,saddr,cb]() {
            auto* op = new iocp_connect_op{};
            op->loop = loop;
            op->cb = cb;
            op->remote_addr = saddr;
            op->sock = new ssocket({});
            op->init();
            op->post_connect();
        });
    }
};