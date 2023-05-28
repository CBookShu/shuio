#include "shuio/shu_client.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "win32_detail.h"

#include <thread>
#include <future>

namespace shu {

    struct iocp_connect_op : sloop_runable {
        sloop* loop;
        OVERLAPPED complete{};
        ssocket* sock;
        connect_runable* cb;
        addr_storage_t remote_addr;
        addr_storage_t local_addr;

        std::thread t;
        int res{0};

        ~iocp_connect_op() {
            if(sock) {
                delete sock;
            }
            t.join();
        }
        void post_connect() {
            t = std::thread([this](){
                struct sockaddr_in addr{};
                storage_2_sockaddr(&remote_addr, &addr);

                do {
                    auto* navite_sock = navite_cast_ssocket(sock);
                    int ret = ::connect(navite_sock->s, (struct sockaddr*)&addr, sizeof(addr));
                    if(ret == SOCKET_ERROR) {
                        res = s_last_error();
                        break;
                    }
                    struct sockaddr_in client_addr;
                    socklen_t addrlen = sizeof(client_addr);
                    if (getsockname(navite_sock->s, (struct sockaddr *)&client_addr, &addrlen) == -1) {
                        res = s_last_error();
                        break;
                    }
                    sockaddr_2_storage(&client_addr, &local_addr);
                }while(0);
                loop->post(this);
            });
        }
        virtual void run() noexcept override {
            if(res != 0) {
                socket_io_result res{.bytes = 0, .err = 1, .naviteerr = this->res};
                cb->run(res, nullptr, {});
                cb->destroy();
            } else {
                socket_io_result res{.err = 0};
                auto* tmp =std::exchange(sock, nullptr);
                addr_pair_t addr_pair{.remote = remote_addr, .local = local_addr};
                cb->run(res, tmp, addr_pair);
                cb->destroy();
            }
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
            op->cb = std::move(cb);
            op->sock = new ssocket({});
            op->sock->init(0);
            op->post_connect();
        });
    }
};