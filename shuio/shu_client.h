#pragma once
#include "shu_common.h"
#include "shu_stream.h"
#include <functional>

namespace shu {

    class sloop;
    class ssocket;

    void shu_connect(sloop*,
    addr_storage_t,
    std::function<void(socket_io_result, ssocket*, addr_pair_t)>);
};