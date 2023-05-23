#pragma once
#include <liburing.h>
#include <sys/socket.h>
#include <cstdint>


namespace shu {

	typedef struct sock_navite_t {
		int fd;
	}sock_navite_t;

    typedef struct uring_navite_t {
        struct io_uring ring;
    }uring_navite_t;
    
    enum class op_type : __u8 {
        type_stop,
        type_io,
        type_run
    };

    struct uring_callback {
        virtual ~uring_callback() {};
        virtual void run(io_uring_cqe*) noexcept = 0;
    };

    typedef struct uring_ud_t {
        op_type type;
        __u8 pad;
        __u32 seq;
    }uring_ud_t;
    static_assert(sizeof(uring_ud_t) == sizeof(io_uring_sqe::user_data));

    typedef struct uring_ud_io_t : uring_ud_t {
        uring_callback* cb;
    }uring_ud_io_t;


    class ssocket;
	auto navite_cast_ssocket(ssocket*) -> sock_navite_t*;

    class sloop;
    auto navite_cast_sloop(sloop*) -> uring_navite_t*;
}