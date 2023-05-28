#pragma once
#include "shu_common.h"
#include "shu_stream.h"
#include <functional>

namespace shu {

    class sloop;
    class ssocket;
    struct connect_runable;

    void shu_connect(sloop*,
    addr_storage_t,
    connect_runable* cb);

    struct connect_runable {
        virtual ~connect_runable() {};
        virtual void run(socket_io_result, ssocket*, addr_pair_t) noexcept = 0;
        virtual void destroy() noexcept {
            delete this;
        }
    };
};