#pragma once
#include "shu_common.h"
#include "shu_stream.h"
#include <functional>

namespace shu {

    class sloop;
    class ssocket;

    class sclient {
        struct sclient_t;
        sclient_t* s_;
        S_DISABLE_COPY(sclient);
    public:
        using func_connect_t = std::function<void(socket_io_result, UPtr<ssocket>, const addr_pair_t&)>;
        using func_close_t = std::function<void(sclient*)>;

        struct sclient_ctx {
			func_close_t evClose;
			func_connect_t evConn;
		};

        sclient();
        sclient(sclient&& other) noexcept;
        ~sclient();

        int start(sloop*, addr_storage_t, sclient_ctx&&);
        auto loop() -> sloop*;
        void stop();
    };
};