#include "shuio/shu_loop.h"
#include "linux_detail.h"

#include <sys/epoll.h>

#include <iostream>
#include <cstring>
#include <cassert>
#include <utility>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>

namespace shu {
    struct uring_ud_run_t : uring_ud_t {
        sloop_runable* cb;
    };

    uring_ud_t g_stop{};
    uring_ud_t g_timer{};

    struct local_timer {
		std::atomic_uint64_t timerseq{ 0 };
		std::map<tp_t, std::unordered_map<std::uint64_t, sloop_runable*>> timers;

        sloop* loop;
		void _set_timer(auto now, bool create) {
			using namespace std::chrono;
			auto expire = loop->handle()->opt.max_expired_time + now;
			expire = timer_min_expire(expire);
			micsec_t diff = duration_cast<micsec_t>(expire - now);
            diff = std::min<>(diff, loop->handle()->opt.max_expired_time);
            io_uring_push_sqe(loop, [&,this](io_uring* u){
                struct __kernel_timespec ts;
                msec_to_ts(&ts, duration_cast<milsec_t>(diff).count());
                auto* sqe = io_uring_get_sqe(u);
                io_uring_sqe_set_data(sqe, &g_timer);
                if(create) {
                    io_uring_prep_timeout(sqe, &ts, 0, 0);
                } else {
                    io_uring_prep_timeout_update(sqe, &ts, (__u64)(&g_timer), 0);
                }
                io_uring_submit(u);
            });
		}

		void start_timer() {
            _set_timer(systime_t::now(), true);
		}
		void timer_call() {
			auto now = systime_t::now();
			auto e = timers.upper_bound(now);
			std::vector< sloop_runable*> runs;
			for (auto b = timers.begin(); b != e; ++b) {
				for (auto& it : b->second) {
					runs.push_back(it.second);
				}
			}
			// !! 这里哪怕runs是空的，也要重新再定时一次！
			// 因为timer的定时器和now的判断未必那么准确！
			timers.erase(timers.begin(), e);
			for (auto& r : runs) {
				r->run();
				r->destroy();
			}
			// 重新定时
			_set_timer(now, true);
		}
		void stop_timer() {
            for (auto& it1 : timers) {
				for (auto& it2 : it1.second) {
					it2.second->destroy();
				}
			}
			timers.clear();
		}
		auto timer_get_seq() -> std::uint64_t {
			for (;;) {
				auto r = timerseq.fetch_add(1);
				if (r == 0) {
					continue;
				}
				return r;
			}
		}
		auto timer_min_expire(tp_t expire) -> tp_t {
			auto it = timers.begin();
			if (it == timers.end()) {
				return expire;
			}
			return std::min<>(it->first, expire);
		}
		auto timer_add(tp_t now, sloop_timer_t_id id, sloop_runable* r) -> void {
			using namespace std::chrono;
			auto expire = timer_min_expire(id.expire);
			timers[id.expire][id.id] = r;
			_set_timer(now, false);
		}
		auto timer_erase(sloop_timer_t_id id) -> void {
			auto it = timers.find(id.expire);
			if (it == timers.end()) {
				return;
			}
			auto it1 = it->second.find(id.id);
			if (it1 == it->second.end()) {
				return;
			}
			it1->second->destroy();
			it->second.erase(it1);
			if (it->second.empty()) {
				timers.erase(it);
			}
			_set_timer(systime_t::now(), false);
		}
	};


    struct sloop::sloop_t : public uring_navite_t {
        struct sloop_opt opt;
        local_timer timer;
        std::thread::id cid;
        std::atomic_uint64_t seq;

        int efd;        // epoll fd
        int pipes[2];   // 用于通讯的pipes
        // timer todo
    };

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
        _loop->timer.loop = this;
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
        auto* ud = new uring_ud_run_t{};
        ud->seq = _loop->seq.fetch_add(1);
        ud->type = op_type::type_run;
        ud->cb = r;

        io_uring_push_sqe(this, [&](io_uring* u){
            auto* sqe = io_uring_get_sqe(u);
            io_uring_sqe_set_data(sqe,ud);
            io_uring_submit(u);
        });
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

    auto sloop::add_timer(sloop_runable* r, std::chrono::milliseconds time) -> sloop_timer_t_id {
        auto now = systime_t::now();

		sloop_timer_t_id id;
		id.id = _loop->timer.timer_get_seq();
		id.expire = now + time;

		auto deletor_f = [](auto* t) {
			t->destroy(); 
		};
		std::unique_ptr<sloop_runable, decltype(deletor_f)> uptr_r(r, deletor_f);
		auto ret = dispatch_f([now, id, time, this, uptr_r = std::move(uptr_r)]()mutable{
			_loop->timer.timer_add(now, id, uptr_r.release());
		});
		if (!ret) {
			return {};
		}
		return id;
    }

    auto sloop::cancel_timer(sloop_timer_t_id id)->void {
		dispatch_f([id, this]() {
			_loop->timer.timer_erase(id);
		});
    }

    auto sloop::run() -> void {
        _loop->cid = std::this_thread::get_id();
        _loop->seq = 0;
        _loop->timer.start_timer();
        for(;;) {
            struct io_uring_cqe *cqe;
            unsigned head;
            unsigned count = 0;
            bool stop = false;
            io_uring_wait_cqe(&_loop->ring, &cqe);
            io_uring_for_each_cqe(&_loop->ring, head, cqe) {
                count++;
                if(cqe->user_data == LIBURING_UDATA_TIMEOUT) {
                    continue;
                }
                uring_ud_t* ud = reinterpret_cast<uring_ud_t*>(cqe->user_data);
                if (ud == &g_stop) {
                    stop = true;
                } else if(ud == &g_timer) {
                    _loop->timer.timer_call();
                } else {
                    if (ud->type == op_type::type_io) {
                        uring_ud_io_t* ud_ptr = static_cast<uring_ud_io_t*>(ud);
                        auto finally = S_DEFER(delete ud_ptr;);
                        ud_ptr->cb->run(cqe);
                    } else if(ud->type == op_type::type_run) {
                        uring_ud_run_t* ud_ptr = static_cast<uring_ud_run_t*>(ud);
                        auto finally = S_DEFER(ud_ptr->cb->destroy(););
                        ud_ptr->cb->run();
                    }   
                }
            }
            io_uring_cq_advance(&_loop->ring, count);

            if(stop) {
                break;
            }
        }
        _loop->timer.stop_timer();
        _loop->cid = {};
        io_uring_queue_exit(&_loop->ring);
        _init_io_uring(this);
    }

    auto sloop::stop() -> void {
        io_uring_push_sqe(this, [&](io_uring* u){
            auto* sqe = io_uring_get_sqe(u);
            io_uring_sqe_set_data(sqe, &g_stop);
            io_uring_submit(u);
        });
    }
};