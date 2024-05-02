#include "shuio/shu_acceptor.h"
#include "linux_detail.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_loop.h"

#include <netinet/in.h>
#include <arpa/inet.h>

namespace shu {
    struct acceptor;
    struct sacceptor::sacceptor_t{
		sloop* loop_;
        sacceptor* owner_;
		std::unique_ptr<ssocket> sock_;
        sacceptor::event_ctx cb_ctx_;
        addr_pair_t addr_pair_;
        sockaddr_storage addr_acceptr_;

        bool op_;
        bool stop_;
        bool close_;

        sacceptor_t(sloop* loop,sacceptor* owner, sacceptor::event_ctx cb_ctx, addr_storage_t addr)
        : loop_(loop),owner_(owner), cb_ctx_(std::forward<event_ctx>(cb_ctx)),addr_pair_{.local=addr},
        op_(false),stop_(false), close_(false)
        {
            shu::panic(!!cb_ctx_.evConn);
        }

        ~sacceptor_t() {
            
        }

        void post_to_close() {
            if (std::exchange(close_, true)) {
                return;
            }
            if (cb_ctx_.evClose) {
                loop_->post([f = std::move(cb_ctx_.evClose), owner = owner_](){
                    f(owner);
                });
            }
        }

        int start() {
            // 先创建 sock和对应的bind和listen
            shu::storage_2_sockaddr(&addr_pair_.local, &addr_acceptr_);
            sock_ = std::make_unique<ssocket>();
            sock_->init(false, addr_acceptr_.ss_family == AF_INET6);
            socklen_t len = addr_acceptr_.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
            if (int err = sock_->bind(&addr_acceptr_, len); err <= 0) {
                socket_io_result_t res{ .res = err };
				cb_ctx_.evConn(owner_, res, nullptr, addr_pair_);
                return err;
            }
            if (int err = sock_->listen(); err <= 0) {
                socket_io_result_t res{ .res = err };
				cb_ctx_.evConn(owner_, res, nullptr, addr_pair_);
                return err;
            }

            sock_->reuse_addr(true);
			sock_->noblock(true);

            navite_fd_setcallback(sock_.get(), [this](io_uring_cqe* cqe){
                run(cqe);
            });
            return post_accept();
        }

        int post_accept() {
            io_uring_push_sqe(loop_, [&](io_uring* ring){
                auto* navie_sock = navite_cast_ssocket(sock_.get());
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                
                socklen_t len = addr_acceptr_.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
                io_uring_prep_accept(sqe, navie_sock->fd, (struct sockaddr*)&addr_acceptr_, &len, 0);
                io_uring_sqe_set_data(sqe, &navie_sock->tag);
                io_uring_submit(ring);
            });

            op_ = true;
            return 1;
        }

        void run(io_uring_cqe* cqe) {
            op_ = false;

            // 有链接过来
            socket_io_result_t res{.res = 1};
            std::unique_ptr<ssocket> sock;
            if(cqe->res >= 0) {
                // 有效值
                sock.reset(new ssocket({}));
                auto* navie_sock = navite_cast_ssocket(sock.get());
                navie_sock->fd = cqe->res;

                sockaddr_storage addrrmote;
                socklen_t len = addr_acceptr_.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
                if (0 == getsockname(cqe->res, (sockaddr*)&addrrmote, &len)) {
                    shu::sockaddr_2_storage(&addrrmote, &addr_pair_.remote);
                } else {
                    addr_pair_.remote = {};
                }
            } else {
                res.res = -s_last_error();
            }

            cb_ctx_.evConn(owner_, res, sock.release(), addr_pair_);

            if (stop_) {
                post_to_close();
            } else {
                post_accept();
            }
        }

        void stop() {
            if (std::exchange(stop_, true)) {
                return;
            }

            if(op_) {
                io_uring_push_sqe(loop_, [&](io_uring* ring){
                    auto* navie_sock = navite_cast_ssocket(sock_.get());
                    struct io_uring_sqe *read_sqe = io_uring_get_sqe(ring);
                    io_uring_prep_cancel(read_sqe, &navie_sock->tag, 0);
                    io_uring_submit(ring);
                });
            } else {
                post_to_close();
            }
        }
    };

    sacceptor::sacceptor():s_(nullptr) {
    }

    sacceptor::sacceptor(sacceptor&& other) noexcept
	{
        s_ = std::exchange(other.s_, nullptr);
	}

    sacceptor::~sacceptor() {
        if (s_) {
            delete s_;
        }
    }

    int sacceptor::start(sloop* loop,event_ctx&& cb_ctx,addr_storage_t addr) {
        shu::panic(!s_);
        s_ = new sacceptor_t(loop, this, std::forward<event_ctx>(cb_ctx), addr);
        return s_->start();
    }

    void sacceptor::stop() {
        shu::panic(s_);
        s_->stop();
    }
};