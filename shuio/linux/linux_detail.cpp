#include "linux_detail.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_loop.h"

namespace shu {
    struct global_init {
        global_init() {
            // 默认就给他处理掉吧，否则业务层还要判断平台再做操作。
            ::signal(SIGPIPE, SIG_IGN);
        }
        ~global_init() {
            
        }
    }G_wsa_initor;

    auto navite_cast_ssocket(ssocket* s) -> fd_navite_t* {
        return reinterpret_cast<fd_navite_t*>(s->handle());
    }
    auto navite_cast_sloop(sloop *loop) -> uring_navite_t *
    {
        return reinterpret_cast<uring_navite_t*>(loop->handle());
    }

    void util_loop_register::register_loop_cb(sloop* loop, ssocket* s, register_event_cb_t&& cb) {
        auto* nl = navite_cast_sloop(loop);
        auto* ns = navite_cast_ssocket(s);
        shu::panic(ns->register_id == 0);
        ns->register_id = nl->make_register_id();
        nl->register_event_cb(ns->register_id, std::forward<register_event_cb_t>(cb));
    }

    void util_loop_register::register_loop_cb(sloop* loop, __u64 id, register_event_cb_t&& cb) {
        auto* nl = navite_cast_sloop(loop);
        shu::panic(id <= MAX_ID_LEFT);
        nl->register_event_cb(id, std::forward<register_event_cb_t>(cb));
    }

    void util_loop_register::unregister_loop(sloop* loop, __u64 id) {
        auto* nl = navite_cast_sloop(loop);
        shu::panic(id != 0);
        nl->unregister_event_cb(id);
    }

    void util_loop_register::unregister_loop(sloop* loop, ssocket* s) {
        auto* nl = navite_cast_sloop(loop);
        auto* ns = navite_cast_ssocket(s);
        shu::panic(ns->register_id != 0);
        nl->unregister_event_cb(ns->register_id);
        ns->register_id = 0;
    }

    __u64 util_loop_register::ud_pack(ssocket* s, int eventid) {
        auto* ns = navite_cast_ssocket(s);
        shu::panic(ns->register_id != 0);
        
        shu::panic(ns->register_id <= MAX_ID_LEFT);
        shu::panic(eventid <= MAX_ID_RIGHT);
        return eventid | (ns->register_id << ID_FLAGS_SIZE);
    }

}