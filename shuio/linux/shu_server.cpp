#include "shuio/shu_server.h"
#include "linux_detail.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_loop.h"

#include <netinet/in.h>
#include <arpa/inet.h>

namespace shu {
    struct acceptor;
    struct sserver::sserver_t : std::enable_shared_from_this<sserver_t>{
		sloop* loop_;
		std::unique_ptr<ssocket> sock_;
        sserver::func_newclient_t creator_;
        struct sockaddr_in addr_in_client_;
        socklen_t addr_size_ = sizeof(addr_in_client_);
        addr_storage_t addr_;
        bool stop_;

        std::shared_ptr<sserver::sserver_t> holder_;

        sserver_t(sloop* loop, sserver::func_newclient_t creator, addr_storage_t addr) {
            addr_ = addr;
            loop_ = loop;
            creator_ = creator;
            stop_ = false;
        }

        void start() {
			// 先创建 sock和对应的bind和listen
			sock_ = std::make_unique<ssocket>();
			sock_->init(addr_.udp == 1);
			sock_->reuse_addr(true);
			sock_->noblock(true);

			if (!addr_.udp) {
				if (!sock_->bind(addr_)) {
					return;
				}
				if (!sock_->listen()) {
					return;
				}
			}
			else {
				// TODO: UDP 直接进行read操作
			}
            navite_fd_setcallback(sock_.get(), [this](io_uring_cqe* cqe){
                run(cqe);
            });
            post_accept();
            holder_ = shared_from_this();
        }

        void post_accept() {
            memset(&addr_in_client_, 0, sizeof(addr_in_client_));
            io_uring_push_sqe(loop_, [&](io_uring* ring){
                auto* navie_sock = navite_cast_ssocket(sock_.get());
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                io_uring_prep_accept(sqe, navie_sock->fd, (struct sockaddr*)&addr_in_client_, &addr_size_, 0);
                io_uring_sqe_set_data(sqe, &navie_sock->tag);
                io_uring_submit(ring);
            });
        }

        void run(io_uring_cqe* cqe) {
            // 有链接过来
            socket_io_result_t res{ .err = 0};
            std::unique_ptr<ssocket> sock;
            if(cqe->res >= 0) {
                // 有效值
                sock.reset(new ssocket({}));
                auto* navie_sock = navite_cast_ssocket(sock.get());
                navie_sock->fd = cqe->res;
            } else {
                res.err = 1;
                res.naviteerr = cqe->res;
            }
            addr_pair_t addr;
			addr.remote.port = ntohs(addr_in_client_.sin_port);
            addr.remote.ip.resize(64);
			inet_ntop(AF_INET, &addr_in_client_.sin_addr, addr.remote.ip.data(), addr.remote.ip.size());
			addr.remote.udp = addr_.udp;

			addr.local = addr_;
            creator_(res, std::move(sock), addr);

            post_accept();
        }

        void stop() {
            if (std::exchange(stop_, true)) {
                return;
            }

            io_uring_push_sqe(loop_, [&](io_uring* ring){
                auto* navie_sock = navite_cast_ssocket(sock_.get());
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                io_uring_prep_cancel_fd(sqe, navie_sock->fd, 0);
                io_uring_submit(ring);
            });
            sock_->close();
            auto self = shared_from_this();
            loop_->post([self](){
                self->holder_.reset();
            });
        }
    };

    sserver::sserver() {
    }

    sserver::sserver(sserver&& other) noexcept
	{
        s_.swap(other.s_);
	}

    sserver::~sserver() {
        if(auto sptr = s_.lock()) {
            
        }
    }

    void sserver::start(sloop* loop, func_newclient_t&& creator, addr_storage_t addr) {
        auto sptr = std::make_shared<sserver_t>(loop, std::forward<func_newclient_t>(creator), addr);
        s_ = sptr;

        loop->dispatch([sptr](){
            sptr->start();
        });
    }

    void sserver::stop() {
        if(auto sptr = s_.lock()) {
            sptr->loop_->dispatch([sptr](){
                sptr->stop();
            });
        }
    }
};