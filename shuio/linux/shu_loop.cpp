#include "shuio/shu_loop.h"
#include "linux_detail.h"

#include <sys/epoll.h>

#include <iostream>
#include <cstring>
#include <cassert>
#include <utility>
#include <atomic>
#include <thread>

namespace shu {
    struct uring_ud_run_t : uring_ud_t {
        sloop_runable* cb;
    };

    struct sloop::sloop_t : public uring_navite_t {
        struct sloop_opt opt;
        std::thread::id cid;
        std::atomic_uint64_t seq;
        int efd;        // epoll fd
        int pipes[2];   // 用于通讯的pipes
        // timer todo
    };

    uring_ud_t g_stop{.type = op_type::type_stop};

    static bool _init_io_uring(sloop *loop) {
        auto* _loop = loop->handle();
        auto depths = {2048, 1024, 512, 256, 128};
        for(auto& d:depths) {
            auto r = io_uring_queue_init(d, &_loop->ring, 0);
            if(r < 0) {
                continue;
            }
            return true;
        }
        assert(false);
        return false;
    }

    sloop::sloop(struct sloop_opt opt) {
        _loop = new sloop_t{};
        _loop->opt = opt;
        _loop->ring.ring_fd = -1;
        auto ok = _init_io_uring(this);
        assert(ok);
        _loop->efd = ::epoll_create1(0);
        assert(_loop->efd >= 0);
        auto ret = ::pipe(_loop->pipes);
        assert(ret >= 0);

    }
    sloop::sloop(sloop&& other) noexcept {
        _loop = std::exchange(other._loop, nullptr);
    }
    sloop::~sloop() {
        if(!_loop) {
            return;
        }
        if(_loop->ring.ring_fd != -1) {
            io_uring_queue_exit(&_loop->ring);
        }
        close(_loop->efd);
        close(_loop->pipes[0]);
        close(_loop->pipes[1]);
        delete _loop;
    }

    auto sloop::handle() -> sloop_t* {
        return _loop;
    }

    auto sloop::post(sloop_runable* r) -> bool {
        auto* sqe = io_uring_get_sqe(&_loop->ring);
        auto* ud = new uring_ud_run_t{};
        auto finally = S_DEFER(
            io_uring_sqe_set_data(sqe,ud);
            io_uring_submit(&_loop->ring);
        );

        ud->seq = _loop->seq.fetch_add(1);
        ud->type = op_type::type_run;
        ud->cb = r;
        return true;
    }

    auto sloop::dispatch(sloop_runable* r) -> bool {
        if(_loop->cid == std::this_thread::get_id()) {
            auto finally = S_DEFER(r->destroy(););
            r->run();
            return true;
        }
        return post(r);
    }

    auto sloop::run() -> void {
        _loop->cid = std::this_thread::get_id();
        _loop->seq = 0;
        for(;;) {
            struct io_uring_cqe *cqe;
            unsigned head;
            unsigned count = 0;
            bool stop = false;
            struct __kernel_timespec kts{.tv_sec = -1, .tv_nsec = -1};
            // todo: fresh kts by timer
            io_uring_wait_cqe_timeout(&_loop->ring, &cqe, &kts);
            io_uring_for_each_cqe(&_loop->ring, head, cqe) {
                count++;
                if(cqe->user_data == LIBURING_UDATA_TIMEOUT) {
                    continue;
                }
                uring_ud_t* ud = reinterpret_cast<uring_ud_t*>(cqe->user_data);
                if(ud->type == op_type::type_stop) {
                    stop = true;
                } else if (ud->type == op_type::type_io) {
                    uring_ud_io_t* ud_ptr = static_cast<uring_ud_io_t*>(ud);
                    auto finally = S_DEFER(delete ud_ptr;);
                    ud_ptr->cb->run(cqe);
                } else if(ud->type == op_type::type_run) {
                    uring_ud_run_t* ud_ptr = static_cast<uring_ud_run_t*>(ud);
                    auto finally = S_DEFER(ud_ptr->cb->destroy(););
                    ud_ptr->cb->run();
                }
            }
            io_uring_cq_advance(&_loop->ring, count);

            if(stop) {
                break;
            }
        }
        _loop->cid = {};
        io_uring_queue_exit(&_loop->ring);
        _init_io_uring(this);
    }

    auto sloop::stop() -> void {
        auto* sqe = io_uring_get_sqe(&_loop->ring);
        io_uring_sqe_set_data(sqe, &g_stop);
        io_uring_submit(&_loop->ring);
    }
};