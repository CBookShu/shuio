#pragma once
#include <liburing.h>
#include <sys/socket.h>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <functional>
#include <variant>


namespace shu {
    typedef struct uring_navite_t {
        struct io_uring ring;
        std::mutex sqe_mutex;
        int efd;        // epoll fd
    }uring_navite_t;
    
	struct io_uring_timer_t {};     // 弃用，通过io_uring 的timeout来计算了，每次返回都会调用timer不再精细化控制了
	struct io_uring_stop_t {};
	struct io_uring_wake_t {};
	struct io_uring_socket_t {
		std::function<void(io_uring_cqe*)> cb;
	};

    static_assert(sizeof(void*) <= sizeof(io_uring_sqe::user_data));

    using io_uring_task_union = std::variant<io_uring_timer_t, io_uring_stop_t, io_uring_wake_t, io_uring_socket_t>;
    inline io_uring_task_union tag_timer = {io_uring_timer_t()};
	inline io_uring_task_union tag_stop = {io_uring_stop_t()};
	inline io_uring_task_union tag_wake = {io_uring_wake_t()};

	typedef struct fd_navite_t {
		int fd;
        io_uring_task_union tag = io_uring_socket_t();
	}fd_navite_t;

    class ssocket;
	auto navite_cast_ssocket(ssocket*) -> fd_navite_t*;

    class sloop;
    auto navite_cast_sloop(sloop*) -> uring_navite_t*;

    inline void io_uring_push_sqe(sloop* sl, auto&& f) {
        auto* l = navite_cast_sloop(sl);
        std::scoped_lock guard(l->sqe_mutex);
        f(&l->ring);
    }
    inline auto io_uring_push_sqe(uring_navite_t* l, auto&& f) {
        std::scoped_lock guard(l->sqe_mutex);
        f(&l->ring);
    }

    inline void msec_to_ts(struct __kernel_timespec *ts, auto dur)
    {
        auto milsec = std::chrono::duration_cast<std::chrono::milliseconds>(dur);
        auto msec = milsec.count();
        ts->tv_sec = msec / 1000;
        ts->tv_nsec = (msec % 1000) * 1000000;
    }

    template <typename F>
    inline void navite_fd_setcallback(fd_navite_t* p, F&& f) {
        std::get_if<io_uring_socket_t>(&p->tag)->cb = std::forward<F>(f);
    }
    template <typename F>
    inline void navite_fd_setcallback(ssocket* s, F&& f) {
        auto* p = navite_cast_ssocket(s);
        std::get_if<io_uring_socket_t>(&p->tag)->cb = std::forward<F>(f);
    }
}