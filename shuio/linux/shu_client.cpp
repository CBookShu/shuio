#include "shuio/shu_client.h"
#include "linux_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"

#include <netinet/in.h>
#include <arpa/inet.h>

namespace shu {
    struct sclient::sclient_t
    {
        sloop* loop_;
        sclient* owner_;
        addr_pair_t addr_pair_;
        sockaddr_storage addr_con_;
        sclient_ctx cb_ctx_;
        std::unique_ptr<ssocket> sock_;
        bool stop_;
        bool close_;
        bool op_;

        sclient_t(sloop* loop,sclient* owner, sclient_ctx&& cb_ctx, addr_storage_t addr) 
        : loop_(loop),owner_(owner),addr_pair_{.remote = addr},cb_ctx_(std::forward<sclient_ctx>(cb_ctx)),
        stop_(false), close_(false), op_(false)
        {
            shu::panic(!!cb_ctx_.evConn);
        }

        void post_to_close() {
            if(std::exchange(close_, true)) {
                return;
            }
            util_loop_register::unregister_loop(loop_, sock_.get());
            if(cb_ctx_.evClose) {
                loop_->post([f = std::move(cb_ctx_.evClose), owner=owner_](){
                    f(owner);
                });
            }
        }

        int start() {
            shu::storage_2_sockaddr(&addr_pair_.remote, &addr_con_);
            sock_ = std::make_unique<ssocket>();
            sock_->init(false, addr_con_.ss_family == AF_INET6);
            sock_->noblock(true);

            util_loop_register::register_loop_cb(loop_, sock_.get(),[this](int eventid, io_uring_cqe* cqe){
                run(cqe);
            });

            op_ = true;
            io_uring_push_sqe(loop_, [&](io_uring* ring){
                auto* navite_sock = navite_cast_ssocket(sock_.get());
                auto* sqe = io_uring_get_sqe(ring);
                io_uring_prep_connect(sqe, navite_sock->fd, (struct sockaddr*)&addr_con_, sizeof(addr_con_));
                io_uring_sqe_set_data64(sqe, util_loop_register::ud_pack(sock_.get(), 0));
                io_uring_submit(ring);
            });

            return 1;
        }

        void run(io_uring_cqe* cqe) {
            op_ = false;
            socket_io_result res{.res = 1};
            if(cqe->res < 0) {
                res.res = cqe->res;
            } else {
                res.res = 1;
                sockaddr_storage addr_common;
                socklen_t len = addr_con_.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
                auto* navite_sock = navite_cast_ssocket(sock_.get());
                if (0 == getsockname(navite_sock->fd, (struct sockaddr*)&addr_common, &len)) {
                    sockaddr_2_storage(&addr_common, &addr_pair_.local);
                }
            }

            cb_ctx_.evConn(res, sock_.release(), addr_pair_);

            if(stop_) {
                post_to_close();
            }
        }

        void stop() {
            if(std::exchange(stop_, true)) {
                return;
            }

            if (op_) {
                io_uring_push_sqe(loop_, [&](io_uring* ring){
                    auto* navie_sock = navite_cast_ssocket(sock_.get());
                    struct io_uring_sqe *read_sqe = io_uring_get_sqe(ring);
                    io_uring_prep_cancel64(read_sqe, util_loop_register::ud_pack(sock_.get(), 0), 0);
                    io_uring_submit(ring);
                });
            } else {
                post_to_close();
            }
        }
    };
    

    sclient::sclient(): s_(nullptr)
    {}
    sclient::sclient(sclient&& other) noexcept
    {
        s_ = std::exchange(other.s_, nullptr);
    }
    sclient::~sclient()
    {
        if (s_) {
            delete s_;
        }
    }
    int sclient::start(sloop* loop, addr_storage_t addr, sclient_ctx&& cb_ctx)
    {
        shu::panic(!s_);
        s_ = new sclient_t(loop, this,  std::forward<sclient_ctx>(cb_ctx), addr);
        return s_->start();
    }
    void sclient::stop()
    {
        shu::panic(s_);
        s_->stop();
    }

};