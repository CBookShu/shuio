#include "shuio/shu_loop.h"
#include "win32_detail.h"

#include <thread>
#include <cassert>
#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <mutex>

#include <algorithm>

namespace shu {


    using namespace std::chrono;

    using namespace shu;

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

    struct win32_timer {
        static constexpr std::chrono::microseconds max_expired_time = 5min;
        std::atomic_uint64_t timerseq{ 0 };
        std::vector<timer_node> timers;

        std::jthread timer_thread;
        HANDLE win32_timer_handle = INVALID_HANDLE_VALUE;
        std::atomic_bool stop = false;

        ~win32_timer() {
            if (win32_timer_handle != win32_timer_handle) {
                CloseHandle(win32_timer_handle);
            }
        }

        void _set_timer(auto now, bool first = false) {
            using namespace std::chrono;
            auto expire = max_expired_time + now;
            expire = timer_min_expire(expire);
            auto diff = duration_cast<std::chrono::microseconds>(expire - now);
            if (first || diff < max_expired_time) {
                // QuadPart的单位是100纳秒
                LARGE_INTEGER timeout{};
                timeout.QuadPart = -duration_cast<std::chrono::nanoseconds>(diff).count() / 100;
                auto ms = duration_cast<std::chrono::milliseconds>(max_expired_time);
                LONG p = static_cast<LONG>(ms.count());
                ::SetWaitableTimer(win32_timer_handle,
                    &timeout, p, 0, 0, FALSE);
            }
        }

        void start_timer(auto&& f) {
            if (win32_timer_handle != INVALID_HANDLE_VALUE) {
                return;
            }
            win32_timer_handle = ::CreateWaitableTimer(0, FALSE, 0);
            _set_timer(steady_clock::now(), true);
            timer_thread = std::jthread([this, f = std::move(f)]() {
                while (!stop) {
                    if (::WaitForSingleObject(win32_timer_handle, INFINITE) == WAIT_OBJECT_0) {
                        f();
                    }
                }
            });
        }
        void timer_call() {
            auto now = steady_clock::now();
            std::vector< timer_node> runs;
            timer_node tmp;
            tmp.id.expire = now;
#undef max
            tmp.id.id = std::numeric_limits<std::uint64_t>::max();
            auto it = std::upper_bound(timers.begin(), timers.end(), tmp);
            runs.assign(timers.begin(), it);
            // !! 这里哪怕runs是空的，也要重新再定时一次！
            // 因为timer的定时器和now的判断未必那么准确！
            timers.erase(timers.begin(), timers.begin() + runs.size());
            for (auto& r : runs) {
                r.cb();
            }
            // 重新定时
            _set_timer(now);
        }
        void stop_timer() {
            stop = true;
            LARGE_INTEGER timeout;
            timeout.QuadPart = 1;
            ::SetWaitableTimer(win32_timer_handle, &timeout, 1, 0, 0, FALSE);
            timer_thread.join();
            ::CloseHandle(win32_timer_handle);
            win32_timer_handle = INVALID_HANDLE_VALUE;
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
        auto timer_min_expire(steady_clock::time_point expire) -> steady_clock::time_point {
            auto it = timers.begin();
            if (it == timers.end()) {
                return expire;
            }
            return std::min<>(it->id.expire, expire);
        }
        auto timer_add(steady_clock::time_point now, sloop_timer_t_id id, func_t&& cb) -> void {
            using namespace std::chrono;
            auto expire = timer_min_expire(id.expire);
            timers.push_back(timer_node{
                .id = id, .cb = std::forward<func_t>(cb)
                });
            std::sort(timers.begin(), timers.end());
            _set_timer(now);
        }
        auto timer_erase(sloop_timer_t_id id) -> void {
            auto it = std::equal_range(timers.begin(), timers.end(), id);
            if (it.first == it.second) return;
            timers.erase(it.first, it.second);
            _set_timer(steady_clock::now());
        }
    };

    struct sloop::sloop_t : iocp_navite_t {
        std::thread::id run_tid_;

        // 定时器
        win32_timer timers_;

        // 自定义函数队列
        std::mutex mutex_;
        std::vector< func_t> tasks_;

        // 增加task running 标志，减轻post的负载
        bool wake_up_{ false };
        bool task_running_{ false };

        sloop_t() {
            iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
            shu::exception_check(iocp != INVALID_HANDLE_VALUE,
                std::format("Bad Create Error:{}", WSAGetLastError()));

            run_tid_ = std::this_thread::get_id();
        }
        ~sloop_t() {
            // 如果run了，那么肯定要先退出run，才能析构
            // 所以到了这里资源应该都已经被销毁才对的，如果iocp 等变量仍然在使用
            // 那就说明loop 的生命周期超过了run函数本身,基本上就是多线程的释放导致的
            // 但是如果没有run直接析构，还是需要释放iocp_
            if (iocp != INVALID_HANDLE_VALUE) {
                CloseHandle(iocp);
            }
        }

        void run() {
            if (iocp == INVALID_HANDLE_VALUE) {
                iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
                shu::exception_check(iocp != INVALID_HANDLE_VALUE,
                    std::format("Bad Create Error:{}", WSAGetLastError()));
            }

            shu::exception_check(std::this_thread::get_id() == run_tid_,
                std::format("run tid != loop create tid"));

            on_start();

            std::vector<OVERLAPPED_ENTRY> overlappeds(1024);
            ULONG count;
            for (;;) {
                BOOL r = ::GetQueuedCompletionStatusEx(
                    iocp,
                    overlappeds.data(),
                    overlappeds.size(),
                    &count,
                    INFINITE,
                    false);
                if (!r) {
                    // timeout?
                    int e = WSAGetLastError();
                    continue;
                }

                wake_up_ = true;

                bool hasstop = false;
                bool hastimer = false;
                for (ULONG i = 0; i < count; ++i) {
                    OVERLAPPED_ENTRY& entry = overlappeds[i];
                    auto* type = (iocp_task_union*)(entry.lpCompletionKey);
                    std::visit
                    (
                        overload
                        (
                            [&](iocp_timer_t&) {hastimer = true;  },
                            [&](iocp_stop_t&) {hasstop = true; },
                            [&](iocp_wake_t& arg) {},
                            [&](iocp_socket_t& arg) { arg.cb(&entry); }
                        ),
                        *type
                    );
                    /*
                        iocp_timer_t,iocp_stop_t,iocp_func_t 都是静态变量，无内存分配；
                        iocp_socket_t 它的内存由IO外部进行控制
                    */
                }

                on_tasks();

                if (hastimer) {
                    on_timer();
                }

                wake_up_ = false;
                // 要退出了，其实还需要清理一些内容才对
                if (hasstop) {
                    // iocp_的生命周期，仅在run函数中
                    CloseHandle(iocp);
                    iocp = INVALID_HANDLE_VALUE;

                    on_stop();
                    break;
                }
                if (count == overlappeds.size()) {
                    overlappeds.resize(count * 2);
                }
            }
        }

        void stop() {
            ::PostQueuedCompletionStatus(iocp, 0, reinterpret_cast<ULONG_PTR>(&tag_stop), nullptr);
        }

        void post(func_t&& cb) {
            shu::exception_check(iocp != INVALID_HANDLE_VALUE);
            {
                std::lock_guard<std::mutex> guard(mutex_);
                tasks_.emplace_back(std::forward<func_t>(cb));
            }

            if (run_tid_ != std::this_thread::get_id()
                || !wake_up_
                || task_running_) {
                // 应该永远不应该失败,哪怕失败了，也无所谓，tasks_ 会负责回收 cb
                BOOL res = ::PostQueuedCompletionStatus(iocp, 0, reinterpret_cast<ULONG_PTR>(&tag_wake), nullptr);
            }
        }

        void dispatch(func_t&& cb) {
            if (std::this_thread::get_id() == run_tid_) {
                std::forward<func_t>(cb)();
                return;
            }
            post(std::forward<func_t>(cb));
        }

        auto add_timer(func_t&& cb, std::chrono::milliseconds time) -> sloop_timer_t_id {
            auto now = steady_clock::now();
            sloop_timer_t_id timer_id = {
                .expire = now + time,
                .id = timers_.timer_get_seq(),
            };

            auto pf = std::function([now, timer_id, this, cb = std::forward<func_t>(cb)]() mutable {
                timers_.timer_add(now, timer_id, std::move(cb));
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
            });
        }

        void on_timer() {
            timers_.timer_call();
        }

        void on_tasks() {
            task_running_ = true;
            std::vector<func_t> pendings;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                pendings.swap(tasks_);
            }
            for (auto& it : pendings) {
                it();
            }
            task_running_ = false;
        }

        void on_start() {
            timers_.start_timer([&] {
                ::PostQueuedCompletionStatus(iocp, 0, reinterpret_cast<ULONG_PTR>(&tag_timer), nullptr);
            });
            wake_up_ = false;
        }

        void on_stop() {
            timers_.stop_timer();

            std::vector<func_t> pendings;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                pendings.swap(tasks_);
            }
            // 哪怕stop了，也把待执行的任务完成一下吧
            for (auto& it : pendings) {
                it();
            }
        }

        void check_thread(std::source_location call) {
            shu::exception_check(std::this_thread::get_id() == run_tid_, "", call);
        }
    };

    sloop::sloop()
    {
        loop_ = new sloop_t{};
    }

    sloop::sloop(sloop&& other) noexcept
    {
        loop_ = std::exchange(other.loop_, nullptr);
    }

    sloop::~sloop()
    {
        if (!loop_) return;
        delete loop_;
    }

    auto sloop::handle() -> sloop_t*
    {
        shu::exception_check(loop_ != nullptr);
        return loop_;
    }

    auto sloop::post(func_t&& cb) -> void
    {
        return loop_->post(std::forward<func_t>(cb));
    }

    auto sloop::dispatch(func_t&& cb) -> void
    {
        return loop_->dispatch(std::forward<func_t>(cb));
    }

    auto sloop::add_timer(func_t&& cb, std::chrono::milliseconds time) -> sloop_timer_t_id
    {
        return loop_->add_timer(std::forward<func_t>(cb), time);
    }

    auto sloop::cancel_timer(sloop_timer_t_id timer_id) -> void
    {
        return loop_->cancel_timer(timer_id);
    }

    void sloop::run()
    {
        loop_->run();
    }

    void sloop::stop()
    {
        loop_->stop();
    }

    void sloop::assert_thread(std::source_location call)
    {
        return loop_->check_thread(std::move(call));
    }

};