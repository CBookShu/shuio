#include "shuio/shu_client.h"
#include "linux_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"

#include <netinet/in.h>
#include <arpa/inet.h>

namespace shu {
    struct sclient::sclient_t : std::enable_shared_from_this<sclient_t>
    {
        sloop* loop_;
        addr_storage_t addr_;
        struct sockaddr_in local_addr_;
        func_connect_t callback_;
        std::unique_ptr<ssocket> sock_;
        std::shared_ptr<sclient_t> holder_;
        bool stop_;

        sclient_t(sloop* loop, func_connect_t&& callback, addr_storage_t addr) {
            loop_ = loop;
            stop_ = false;
            addr_ = addr;
            callback_ = std::forward<func_connect_t>(callback);
        }

        void start() {
            sock_ = std::make_unique<ssocket>();
            sock_->init(addr_.udp);
            navite_fd_setcallback(sock_.get(), [this](io_uring_cqe* cqe){
                run(cqe);
            });

            io_uring_push_sqe(loop_, [&](io_uring* ring){
                auto* navite_sock = navite_cast_ssocket(sock_.get());
                auto* sqe = io_uring_get_sqe(ring);
                storage_2_sockaddr(&addr_, &local_addr_);
                io_uring_prep_connect(sqe, navite_sock->fd, (struct sockaddr*)&local_addr_, sizeof(sockaddr_in));
                io_uring_sqe_set_data(sqe, &navite_sock->tag);
                io_uring_submit(ring);
            });

            holder_ = shared_from_this();
        }

        void run(io_uring_cqe* cqe) {
            if(cqe->res < 0) {
                socket_io_result res{.bytes = 0, .err = 1, .naviteerr = cqe->res};
                callback_(res, nullptr, addr_pair_t{.remote = addr_,.local = {}});
            } else {
                socket_io_result res{.bytes = 0, .err = 0, .naviteerr = 0};
                struct sockaddr_in addr;
                socklen_t len = sizeof(addr);
                auto* navite_sock = navite_cast_ssocket(sock_.get());
                addr_pair_t addr_pair{
                    .remote = addr_
                };
                if (0 == getsockname(navite_sock->fd, (struct sockaddr*)&addr, &len)) {
                    sockaddr_2_storage(&addr, &addr_pair.local);
                }
                callback_(res, std::move(sock_), addr_pair);
            }

            holder_.reset();
        }

        void stop() {
            if(std::exchange(stop_, true)) {
                return;
            }

            io_uring_push_sqe(loop_, [&](io_uring* ring){
                auto* navie_sock = navite_cast_ssocket(sock_.get());
                struct io_uring_sqe *read_sqe = io_uring_get_sqe(ring);
                io_uring_prep_cancel(read_sqe, &navie_sock->tag, 0);
                io_uring_submit(ring);
            });
        }
    };
    

    sclient::sclient()
    {}
    sclient::sclient(sclient&& other) noexcept
    {
        s_.swap(other.s_);
    }
    sclient::~sclient()
    {
        auto sptr = s_.lock();
        if (sptr) {

        }
    }
    void sclient::start(sloop* loop, addr_storage_t saddr, func_connect_t&& cb)
    {
        auto sptr = std::make_shared<sclient_t>(loop, std::forward<func_connect_t>(cb), saddr);
        s_ = sptr;
        loop->dispatch([sptr]() {
            sptr->start();
        });
    }
    void sclient::stop()
    {
        if (auto sptr = s_.lock()) {
            sptr->loop_->dispatch([sptr]() {
                sptr->stop();
            });
        }
    }

};