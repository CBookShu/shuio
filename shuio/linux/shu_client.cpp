#include "shuio/shu_client.h"
#include "linux_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"

#include <netinet/in.h>
#include <arpa/inet.h>

namespace shu {


    struct uring_connect_op : uring_callback {
        uring_ud_io_t complete;
        ssocket* sock;
        sloop* loop;

        addr_storage_t remote_addr;
        struct sockaddr_in local_addr;
        connect_runable* cb;

        ~uring_connect_op() {
            if(sock) {
                delete sock;
            }
        }

        void post_connect() {
            // 进行connect操作
            io_uring_push_sqe(loop, [&](io_uring* ring){
                auto* navite_sock = navite_cast_ssocket(sock);
                auto* sqe = io_uring_get_sqe(ring);
                storage_2_sockaddr(&remote_addr, &local_addr);
                io_uring_prep_connect(sqe, navite_sock->fd, (struct sockaddr*)&local_addr, sizeof(sockaddr_in));
                io_uring_sqe_set_data(sqe, &complete);
                io_uring_submit(ring);
            });
        }

        virtual void run(io_uring_cqe* cqe) noexcept override {
            if(cqe->res < 0) {
                socket_io_result res{.err = 1, .naviteerr = cqe->res};
                cb->run(res, nullptr, {});
                cb->destroy();
                delete this;
            } else {
                socket_io_result res{.err = 0};
                auto* tmp = std::exchange(sock, nullptr);
                addr_pair_t addr_pair;
                addr_pair.remote = remote_addr;
                sockaddr_2_storage(&local_addr, &addr_pair.local);
                cb->run(res, tmp, addr_pair);
                cb->destroy();
                delete this;
            }
        }
    };

    void shu_connect(sloop* loop, 
    addr_storage_t saddr, 
    connect_runable* cb) 
    {
        loop->dispatch_f([loop,saddr,cb]() {
            auto* op = new uring_connect_op{};
            op->loop = loop;
            op->remote_addr = saddr;
            op->cb =cb;
            op->complete.type = op_type::type_io;
            op->complete.cb = op;
            op->sock = new ssocket({});
            op->sock->init(0);

            op->post_connect();
        });
    }
};