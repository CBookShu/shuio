#pragma once
#include <liburing.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <functional>
#include <variant>
#include <unordered_map>
#include <cassert>

#include "shuio/shu_common.h"


namespace shu {
    // io_uring_cqe::user_data  64 bit 使用如下
    /*              registerid              eventid
        63 ------------------------------ 3--------0
    */
    constexpr int ID_FLAGS_SIZE = 3;
    constexpr std::uint64_t MASK_ID_RIGHT = ((std::uint64_t)1 << ID_FLAGS_SIZE) - 1;
    constexpr std::uint64_t MASK_ID_LEFT = std::numeric_limits<std::uint64_t>::max() - MASK_ID_RIGHT;
    constexpr std::uint64_t MAX_ID_LEFT = MASK_ID_LEFT >> ID_FLAGS_SIZE;
    constexpr std::uint64_t MAX_ID_RIGHT = MASK_ID_RIGHT;

    using register_event_cb_t = std::function<void(int eventid, io_uring_cqe*)>;

    typedef struct uring_navite_t {
        struct io_uring ring;
        int efd;        // epoll fd, 忽略

        // 注册队列id序列(60 bit),永远递增不回头
        __u64 register_id_req{0};
        __u64 make_register_id() {
            return ++register_id_req;
        }

        // 注册回调
        std::unordered_map<__u64, register_event_cb_t> register_cb;
        void register_event_cb(__u64 id, register_event_cb_t&& cb) {
            register_cb.insert_or_assign(id, std::forward<register_event_cb_t>(cb));
        }
        void unregister_event_cb(__u64 id) {
            register_cb.erase(id);
        }
    }uring_navite_t;

	typedef struct fd_navite_t {
		int fd;
        __u64 register_id{0};
	}fd_navite_t;

    class ssocket;
	auto navite_cast_ssocket(ssocket*) -> fd_navite_t*;

    class sloop;
    auto navite_cast_sloop(sloop*) -> uring_navite_t*;

    inline void io_uring_push_sqe(sloop* sl, auto&& f) {
        auto* l = navite_cast_sloop(sl);
        f(&l->ring);
    }
    inline auto io_uring_push_sqe(uring_navite_t* l, auto&& f) {
        f(&l->ring);
    }

    inline void msec_to_ts(struct __kernel_timespec *ts, auto dur)
    {
        auto milsec = std::chrono::duration_cast<std::chrono::milliseconds>(dur);
        auto msec = milsec.count();
        ts->tv_sec = msec / 1000;
        ts->tv_nsec = (msec % 1000) * 1000000;
    }

    struct util_loop_register {
        static void register_loop_cb(sloop* loop, ssocket* s, register_event_cb_t&& cb);
        static void register_loop_cb(sloop* loop, __u64 id, register_event_cb_t&& cb);

        static void unregister_loop(sloop* loop, __u64 id);
        static void unregister_loop(sloop* loop, ssocket* s);
        
        static __u64 ud_pack(ssocket* s, int eventid);
        static __u64 ud_pack(__u64 id, int eventid) {
            shu::panic(id <= MAX_ID_LEFT);
            shu::panic(eventid <= MAX_ID_RIGHT);
            return eventid | (id << ID_FLAGS_SIZE);
        }
        static std::tuple<__u64, int> ud_parse(__u64 ud)  {
            return std::make_tuple(ud >> ID_FLAGS_SIZE, ud & MASK_ID_RIGHT);
        }
    };
}