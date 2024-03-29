#pragma once
#include "shu_common.h"
#include "shu_stream.h"
#include <functional>

namespace shu {

    class sloop;
    class ssocket;

    class sclient {
        struct sclient_t;
        std::weak_ptr<sclient_t> s_;
        S_DISABLE_COPY(sclient);
    public:
        using func_connect_t = std::function<void(socket_io_result, std::unique_ptr<ssocket>, addr_pair_t)>;

        sclient();
        sclient(sclient&& other) noexcept;
        ~sclient();

        void start(sloop*, addr_storage_t, func_connect_t&&);
        void stop();
    };
};