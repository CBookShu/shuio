#include "shuio/shu_loop.h"
#include "linux_detail.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <poll.h>

#include <iostream>
#include <cstring>
#include <cassert>
#include <utility>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <set>

namespace shu {

    using namespace std::chrono;

    std::strong_ordering operator <=> (const sloop_timer_t_id& l, 
        const sloop_timer_t_id& r) {
        if (l.expire == r.expire) {
            return l.id <=> r.id;
        }
        return l.expire <=> r.expire;
    }

    struct timer_node {
        sloop_timer_t_id id;
        func_t cb;

        std::weak_ordering operator <=> (const timer_node& other) const {
            return id <=> other.id;
        }
        std::weak_ordering operator <=> (const sloop_timer_t_id& other) const {
            return id <=> other;
        }
    };

    constexpr auto timer_compare_func = std::greater<timer_node>();

    struct local_timer {
        static constexpr std::chrono::microseconds max_expired_time = 5min;
		std::atomic_uint64_t timerseq{ 0 };
		std::vector<timer_node> timers;
        std::set<sloop_timer_t_id> cancel_slots;

		void timer_call() {
			auto now = steady_clock::now();
            std::vector<func_t> runs;
            while(!timers.empty()) {
                if (timers.front().id.expire <= now) {
                    if(cancel_slots.erase(timers.front().id) == 0) {
                        runs.emplace_back(std::move(timers.front().cb));
                    }
                    std::pop_heap(timers.begin(), timers.end(), timer_compare_func);
                    timers.pop_back();
                } else {
                    break;
                }
            }
            for(auto& cb:runs) {
                cb();
            }
            // cancel_slots 有时候会有冗余，比如删除已经执行过的timer
            if (timers.empty()) {
                cancel_slots.clear();
            } else {
                auto it = cancel_slots.lower_bound(timers.front().id);
                cancel_slots.erase(cancel_slots.begin(), it);
            }
		}
		void stop_timer() {
			timers.clear();
            cancel_slots.clear();
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
		auto timer_min_expire(steady_clock::time_point expire) -> steady_clock::time_point {
			auto it = timers.begin();
            if (it == timers.end()) {
                return expire;
            }
            return std::min<>(it->id.expire, expire);
		}
		auto timer_add(steady_clock::time_point now, sloop_timer_t_id id, func_t&& cb) -> void {
			using namespace std::chrono;
            timers.push_back(timer_node{
                .id = id, .cb = std::forward<func_t>(cb)
                });
            std::push_heap(timers.begin(), timers.end(), timer_compare_func);
		}
		auto timer_erase(sloop_timer_t_id id) -> void {
            cancel_slots.insert(id);
		}
	
        auto timer_wait_leatest() {
            auto now = steady_clock::now();
            auto expire = max_expired_time + now;
			expire = timer_min_expire(expire);
            auto diff = std::chrono::duration_cast<micsec_t>(expire - now);
            diff = std::min<>(diff, max_expired_time);

            struct __kernel_timespec ts;
            msec_to_ts(&ts, diff);
            return ts;
        }
    };

    struct task_node_t {
        func_t cb;
        std::uint64_t id;
    };

    struct sloop::sloop_t : public uring_navite_t {
        local_timer timers_;
        std::thread::id run_tid_;

        // 带锁的队列（不支持cancel)
        std::mutex mutex_;
        std::vector<task_node_t> tasks_mux_;
        std::atomic_uint32_t tasks_count_;
        std::atomic_uint64_t task_req_;

        // 内部的不需要加锁的队列
        std::vector<task_node_t>  taks_nmux_;

        static constexpr int event_id_stop_ = 1;
        static constexpr int event_id_efd_ = 2;
        __u64 register_id_;

        int event_fd_;
        std::uint64_t event_fd_buf_;

        std::any ud_;

        sloop_t() 
        : run_tid_(std::this_thread::get_id()),task_req_(0),register_id_(make_register_id())
        {
            auto depths = {2048, 1024, 512, 256, 128};
            bool ok = false;
            for(auto& d:depths) {
                // 2.6 版本的liburing 终于可以传入flags
                // 使用IORING_SETUP_SQPOLL 可以让liburing的极限性能接近epoll
                auto r = io_uring_queue_init(d, &ring, IORING_SETUP_SQPOLL);
                if(r < 0) {
                    continue;
                }
                ok = true;
                break;
            }
            shu::panic(ok, "liuring init faild");

            if(event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); event_fd_ == -1) {
                shu::panic(false, std::string("eventfd:") + std::to_string(s_last_error()));
            }

            // if (int r = io_uring_register_eventfd(&ring, event_fd_); r != 0) {
            //     shu::panic(false, std::string("io_uring_register_eventfd:") + std::to_string(r));
            // }
        }
        ~sloop_t() {
            if (ring.ring_fd != -1) {
                io_uring_queue_exit(&ring);
            }
            if (event_fd_ != -1) {
                close(event_fd_);
                event_fd_ = -1;
            }
        }
        void run() {
            shu::panic(std::this_thread::get_id() == run_tid_,"run tid != loop create tid");
            on_start();

            for(;;) {
                struct io_uring_cqe *cqe;
                unsigned head;
                unsigned count = 0;
                bool hasstop = false;
                struct __kernel_timespec ts{};
                bool nowait = tasks_mux_.size() > 0 || tasks_count_ > 0;
                if (!nowait) {
                    ts = timers_.timer_wait_leatest();
                }
                io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

                io_uring_for_each_cqe(&ring, head, cqe) {
                    count++;
                    if(cqe->user_data == LIBURING_UDATA_TIMEOUT) {
                        continue;
                    }

                    if(cqe->user_data == 0) {
                        continue;
                    }
                    
                    auto [rid,event_id] = util_loop_register::ud_parse(cqe->user_data);
                    if (rid == register_id_) {
                        // loop system event
                        if (event_id == event_id_efd_) {
                            handle_eventfd();
                            post_read_eventfd();
                        } else if(event_id == event_id_stop_) {
                            hasstop = true;
                        }
                    } else {
                        // user event
                        if (auto it = register_cb.find(rid); it != register_cb.end()) {
                            it->second(event_id, cqe);
                        }
                    }

                }
                io_uring_cq_advance(&ring, count);

                on_tasks();
                on_timer();
                if(hasstop) {
                    break;
                }
            }
        
            on_stop();
        }

        void stop() {
            dispatch([this](){
                io_uring_push_sqe(this, [&](io_uring* u){
                    auto* sqe = io_uring_get_sqe(u);
                    io_uring_prep_nop(sqe);
                    io_uring_sqe_set_data64(sqe, util_loop_register::ud_pack(register_id_, event_id_stop_));
                    io_uring_submit(u);
                });
            });
        }

        void post(func_t&& cb) {
            tasks_count_ ++;
            if (run_tid_ == std::this_thread::get_id()) {
                post_inloop(std::forward<func_t>(cb));
            } else {
                auto id = task_req_++;
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    tasks_mux_.emplace_back(task_node_t{.cb = std::forward<func_t>(cb), .id = id});
                }
                wakeup();
            }
        }

        void dispatch(func_t&& cb) {
            if (std::this_thread::get_id() == run_tid_) {
                std::forward<func_t>(cb)();
                return;
            }
            post(std::forward<func_t>(cb));
        }

        auto post_inloop(func_t&& cb) -> std::uint64_t {
            auto id = task_req_++;
            taks_nmux_.emplace_back(task_node_t{.cb = std::forward<func_t>(cb), .id = id});
            return id;
        }

        void dispatch_inloop(func_t&& cb) {
            std::forward<func_t>(cb)();
        }

        auto add_timer(func_t&& cb, std::chrono::milliseconds time) -> sloop_timer_t_id {
            auto now = steady_clock::now();
            sloop_timer_t_id timer_id = {
                .expire = now + time,
                .id = timers_.timer_get_seq(),
            };

            auto pf = std::function([now, timer_id, this, cb = std::forward<func_t>(cb)]() mutable {
                timers_.timer_add(now, timer_id, std::move(cb));

                // 定时器发生变化，就给loop wake一下，让他重新timeout
                wakeup();
            });

            /*
                dispatch 本身是不会有异常，一定会把pf保存下来，哪怕失败了，最后也会被释放；
                但是pf 后面的lambda 内保存sloop_runable* r，需要使用只能指针
                因为一旦最终pf没有执行，而是被释放了，r需要能正确释放掉
            */
            dispatch(std::move(pf));
            return timer_id;
        }

        void cancel_timer(sloop_timer_t_id timer_id) {
            dispatch([timer_id, this]() {
                timers_.timer_erase(timer_id);

                // 定时器发生变化，就给loop wake一下，让他重新timeout
                wakeup();
            });
        }

        void on_timer() {
            timers_.timer_call();
        }

        void on_start() {
            post_read_eventfd();
        }

        void on_tasks() {
            std::vector<task_node_t> pendings;
            {
                std::lock_guard guard(mutex_);
                pendings.swap(tasks_mux_);
            }
            for(auto& it:pendings) {
                it.cb();
            }
        
            tasks_count_ -= pendings.size();

            std::vector<task_node_t> pendings1;
            pendings1.swap(taks_nmux_);
            for(auto& it:pendings1) {
                it.cb();
            }
        }
        void on_stop() {
            timers_.stop_timer();
            io_uring_queue_exit(&ring);
            ring.ring_fd = -1;

            close(event_fd_);
            event_fd_ = -1;

            on_tasks();
        }

        void post_read_eventfd() {
            shu::panic(event_fd_ != -1);
            auto* sqe = io_uring_get_sqe(&ring);
            io_uring_prep_poll_add(sqe, event_fd_, POLLIN);
            io_uring_sqe_set_data64(sqe, util_loop_register::ud_pack(register_id_, event_id_efd_));
            io_uring_submit(&ring);
        }

        void wakeup() {
            shu::panic(event_fd_ != -1);
            std::uint64_t v = 1;
            int r = ::write(event_fd_, &v, sizeof(v));
            shu::panic(r == sizeof(v));
        }

        void handle_eventfd() {
            std::uint64_t v;
            int r = ::read(event_fd_, &v, sizeof(v));
            shu::panic(r == sizeof(v));
        }

        void check_thread(std::source_location call) {
            shu::panic(std::this_thread::get_id() == run_tid_, "", call);
        }
    };

    sloop::sloop() {
        loop_ = new sloop_t{};
    }
    sloop::sloop(sloop&& other) noexcept {
        loop_ = std::exchange(other.loop_, nullptr);
    }
    sloop::~sloop() {
        if(loop_) {
            delete loop_;
        }
    }

    auto sloop::get_ud() -> std::any* {
        shu::panic(loop_);
        return &loop_->ud_;
    }

    auto sloop::handle() -> sloop_t* {
        return loop_;
    }

    void sloop::post(func_t&& cb){
        return loop_->post(std::forward<func_t>(cb));
    }

    void sloop::dispatch(func_t&& cb){
        return loop_->dispatch(std::forward<func_t>(cb));
    }

    auto sloop::add_timer(func_t&& cb, std::chrono::milliseconds time) -> sloop_timer_t_id {
        return loop_->add_timer(std::forward<func_t>(cb), time);
    }

    auto sloop::cancel_timer(sloop_timer_t_id id)->void {
		return loop_->cancel_timer(id);
    }

    auto sloop::run() -> void {
        return loop_->run();
    }

    auto sloop::stop() -> void {
        return loop_->stop();
    }

    void sloop::assert_thread(std::source_location call) {
        return loop_->check_thread(std::move(call));
    }
};