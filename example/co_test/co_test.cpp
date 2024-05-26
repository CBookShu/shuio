#include <iostream>
#include <coroutine>
#include <variant>
#include <assert.h>
#include <optional>

#include "shuio/shuio.h"

#include "co_result.hpp"

using namespace shu;
using namespace std::chrono_literals;

class co_ctx_t {
    sloop* loop_;
    std::unordered_map<std::uint64_t, std::coroutine_handle<>> register_co_;
    std::uint64_t req_;

public:
    co_ctx_t(sloop* loop):
    loop_(loop),req_(0)
    {}

    auto loop() -> sloop* {
        return loop_;
    }

    std::uint64_t alloc_id() {
        loop_->assert_thread();
        return ++req_;
    }

    bool register_co(std::uint64_t id, std::coroutine_handle<> h) {
        auto it = register_co_.try_emplace(id, h);
        return it.second;
    }

    void unregister_co(std::uint64_t id) {
        register_co_.erase(id);
    }

    std::coroutine_handle<> take_co(std::uint64_t id) {
        if(auto it = register_co_.find(id); it != register_co_.end()) {
            auto r = it->second;
            register_co_.erase(it);
            return r;
        }
        return std::noop_coroutine();
    }
};

struct task_node_t {
    co_ctx_t* co_ctx_;
    task_node_t* pre_;
    std::coroutine_handle<> handle_;
    std::uint64_t id_;
    

    task_node_t(co_ctx_t* ctx, std::coroutine_handle<> handle):
    co_ctx_(ctx),pre_(nullptr),handle_(handle),id_(ctx->alloc_id())
    {}
    ~task_node_t() 
    {}

    std::uint64_t get_id() {return id_;}
    co_ctx_t* ctx() {return co_ctx_;}
    bool reg() { return co_ctx_->register_co(id_, handle_);}
    void unreg() {co_ctx_->unregister_co(id_);}

    template <typename C>
    decltype(auto) delay_resume(C c) {
        auto r = reg();
        assert(r);
        return co_ctx_->loop()->add_timer([id = id_,co_ctx = co_ctx_](){
            auto h = co_ctx->take_co(id);
            h.resume();
        }, c);
        
    }

    void dispatch_resume() {
        auto r = reg();
        assert(r);
        return co_ctx_->loop()->dispatch([id = id_,co_ctx = co_ctx_](){
            auto h = co_ctx->take_co(id);
            h.resume();
        });
    }

    void post_resume() {
        auto r = reg();
        assert(r);
        return co_ctx_->loop()->post([id = id_,co_ctx = co_ctx_](){
            auto h = co_ctx->take_co(id);
            h.resume();
        });
    }
};

template <typename R>
struct future_t {
    struct promise_type;
    using coro_handle = std::coroutine_handle<promise_type>;
    future_t(coro_handle h):handle_(h) {}
    future_t(future_t&& other):handle_(std::exchange(other.handle_, nullptr)) {}
    ~future_t() {
        destroy();
    }

    bool valid() {
        return handle_;
    }
    bool done() {
        return !handle_ || handle_.done();
    }

    decltype(auto) get_result() & {
        return handle_.promise().result();
    }

    decltype(auto) get_result() && {
        return std::move(handle_.promise()).result();
    }

    struct AwaiterBase {
        constexpr bool await_ready() {
            if (self_coro_) [[likely]]
            { return self_coro_.done(); }
            return true;
        }
        template<typename Promise>
        void await_suspend(std::coroutine_handle<Promise> resumer) const noexcept {
            // resumer.promise();
            self_coro_.promise().pre_ = &resumer.promise();
        }
        coro_handle self_coro_ {};
    };

    auto operator co_await() const & noexcept {
        struct Awaiter: AwaiterBase {
            decltype(auto) await_resume() const {
                if (! AwaiterBase::self_coro_) [[unlikely]]
                { std::terminate();}
                return AwaiterBase::self_coro_.promise().result();
            }
        };
        return Awaiter {handle_};
    }

    auto operator co_await() const && noexcept {
        struct Awaiter: AwaiterBase {
            decltype(auto) await_resume() const {
                if (! AwaiterBase::self_coro_) [[unlikely]]
                { std::terminate(); }
                return std::move(AwaiterBase::self_coro_.promise()).result();
            }
        };
        return Awaiter {handle_};
    }

    struct promise_type : task_node_t, result_t<R>{
        template <typename... Args>
        promise_type(co_ctx_t* ctx, Args&&... args):
        task_node_t(ctx, coro_handle::from_promise(*this))
        {};

        template <typename Obj, typename... Args>
        promise_type(Obj&& obj, co_ctx_t* ctx, Args&&... args):
        task_node_t(ctx, coro_handle::from_promise(*this))
        {}

        ~promise_type() = default;

        future_t get_return_object() {
            return future_t{coro_handle::from_promise(*this)};
        }

        constexpr auto initial_suspend() {
            return std::suspend_never{};
        }

        struct Final_Awatier {
            constexpr bool await_ready() const noexcept {return false;}
            constexpr void await_resume() const noexcept {}
            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> h) const noexcept -> std::coroutine_handle<> {
                if (h.promise().pre_) {
                    return h.promise().pre_->handle_;
                }
                return std::noop_coroutine();
            }
        };
        constexpr auto final_suspend() noexcept {
            unreg();
            return Final_Awatier{};
        }
    };

protected:
    void destroy() {
        if (coro_handle co = std::exchange(handle_, nullptr)) {
            // promise cancel
            co.destroy();
        }
    }
private:
    coro_handle handle_;
};

struct co_sleep {
    template <typename C>
    requires std::convertible_to<C, std::chrono::milliseconds>
    co_sleep(C c)
    :timeout_(c) 
    {}

    bool await_ready() const {
        return false;
    }

    template <typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> h) -> std::coroutine_handle<> {
        id_ = h.promise().delay_resume(timeout_);
        if (id_.id > 0) {
            return std::noop_coroutine();
        }
        return h;
    }

    constexpr void await_resume() {

    }

    sloop_timer_t_id id_;
    std::chrono::milliseconds timeout_;
};

struct co_tcpserver {
    co_tcpserver(co_ctx_t* ctx, addr_storage_t addr):
    addr_(addr),cb_(nullptr),close_(0)
    {
        err_ = acceptor_.start(ctx->loop(), {
            .evClose = [this](sacceptor* a){
                on_close();
            },
            .evConn = [this](sacceptor* a, socket_io_result_t res, UPtr<ssocket> sock, addr_pair_t addr) {
                on_client(a, res, std::move(sock), addr);
            }
        }, addr_);
    }
    ~co_tcpserver() {
        
    }

    // Accept
    // Awaiter?
    struct AcceptAwaiter {
        co_tcpserver* parrent_;
        std::tuple<UPtr<ssocket>,addr_pair_t> res_;
        std::coroutine_handle<> h_;
        AcceptAwaiter(co_tcpserver* p):parrent_(p){}
        bool await_ready() { 
            res_ = parrent_->pop();
            if (std::get<0>(res_)) return true;
            if(parrent_->cb_) return true;
            assert(!parrent_->closed());
            return parrent_->err_ <= 0;
        }
        
        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> h) {
            parrent_->cb_ = this;
            h_ = h;
        }

        std::tuple<UPtr<ssocket>,addr_pair_t>  await_resume() {
            return std::move(res_);
        }
    };

    // close
    // Awaiter
    struct StopAwaiter {
        co_tcpserver* parrent_;
        StopAwaiter(co_tcpserver* p):parrent_(p){}
        std::coroutine_handle<> h_;

        bool await_ready() { 
            return parrent_->closed();
        }

        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> h) {
            h_ = h;
            parrent_->cb_stop_ = this;
            parrent_->acceptor_.stop();
        }
        constexpr void await_resume() {
            
        }
    };

    [[nodiscard]]
    AcceptAwaiter co_accept() {
        return AcceptAwaiter(this);
    }

    [[nodiscard]]
    StopAwaiter co_stop() {
        return StopAwaiter(this);
    }

    bool closed() {
        return close_ != 0;
    }
protected:

    void on_close() {
        close_ = 1;
        if(auto cb = std::exchange(cb_stop_, nullptr)) {
            cb->h_.resume();
        }
    }
    void on_client(sacceptor* a, socket_io_result_t res, UPtr<ssocket> sock, addr_pair_t addr) {
        if (res.res <= 0) {
            err_ = res.res;
        }
        
        if(auto cb = std::exchange(cb_, nullptr)) {
            cb->res_ = std::make_tuple(std::move(sock), addr);
            cb->h_.resume();
        } else {
            if(res.res > 0) {
                pendings_.emplace_back(std::move(sock), addr);
            }
        }
    }

    std::tuple<UPtr<ssocket>,addr_pair_t> pop() {
        if (pendings_.empty()) return std::make_tuple(UPtr<ssocket>(), addr_pair_t());
        auto res = std::move(pendings_.front());
        pendings_.pop_front();
        return res;
    }

    std::list<std::tuple<UPtr<ssocket>,addr_pair_t>> pendings_;
    sacceptor acceptor_;
    addr_storage_t addr_;
    AcceptAwaiter* cb_;
    StopAwaiter* cb_stop_;
    int err_;
    int close_;
};

struct co_session {
    co_session(co_ctx_t* ctx, UPtr<ssocket> sock) {
        err_ = stream_.start(ctx->loop(), std::move(sock), {}, {
            .evClose = [this](sstream* s){
                on_close();
            }
        });

        if (err_ <= 0) {
            return;
        }

        stream_.read([this](sstream* s,socket_io_result_t res, buffers_t bufs){
            on_read(s, res, bufs);
        }, [this](sstream* s,buffer_t& buf){
            on_alloc(s, buf);
        });
    }

    // Read
    struct ReadAwaiter {
        co_session* parrent;
        std::coroutine_handle<> h_;
        std::string buf_;
        ReadAwaiter(co_session* p)
        :parrent(p)
        {}

        bool await_ready() {
            if (parrent->err_ <= 0) return true;
            assert(!parrent->closed());

            int readcount = parrent->takebuf(buf_);
            if (readcount > 0) {
                return true;
            }
            return false;
        }

        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> h) {
            h_ = h;
            parrent->read_ = this;
        }
        std::string await_resume() {
            return std::move(buf_);
        }
    };

    // Write
    struct WriteAwaiter {
        co_session* parrent;
        std::coroutine_handle<> h_;
        std::span<char> buf_;
        int write_count_;
        WriteAwaiter(co_session* p, std::span<char> buf)
        :parrent(p),buf_(buf),write_count_(0)
        {}

        bool await_ready() {
            if (parrent->err_ <= 0) return true;
            assert(!parrent->closed());

            parrent->stream_.write(buffer_t{.size = (unsigned long)buf_.size(), .p = buf_.data()}, 
            [this](sstream* s,socket_io_result_t res){
                if (res.res <= 0) {
                    parrent->err_ = res.res;
                    return;   
                }

                write_count_ = res.res;
                if(auto p = std::exchange(parrent->write_, nullptr)) {
                    assert(p == this);
                    assert(p->h_);
                    p->h_.resume();
                }
            });
            if(write_count_ > 0) {
                return true;
            }
            return false;
        }

        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> h) {
            h_ = h;
            parrent->write_ = this;
        }

        int await_resume() {
            return write_count_;
        }
    };

    struct StopAwaiter {
        co_session* parrent_;
        StopAwaiter(co_session* p):parrent_(p){}
        std::coroutine_handle<> h_;

        bool await_ready() { 
            return parrent_->closed();
        }

        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> h) {
            h_ = h;
            parrent_->cb_stop_ = this;
            parrent_->stream_.stop();
        }
        constexpr void await_resume() {
            
        }
    };

    [[nodiscard]]
    ReadAwaiter co_read() {
        return ReadAwaiter(this);
    }

    [[nodiscard]]
    WriteAwaiter co_write(std::span<char> buf) {
        return WriteAwaiter(this, buf);
    }

    [[nodiscard]]
    StopAwaiter co_stop() {
        return StopAwaiter(this);
    }

    void on_read(sstream* s,socket_io_result_t res, buffers_t bufs) {
        if(res.res <= 0) {
            err_ = res.res;
        }

        if(err_ > 0) {
            if (auto p = std::exchange(read_, nullptr)) {
                for(auto& b:bufs) {
                    p->buf_.append(b.p, b.size);
                }
                p->h_.resume();
            } else {
                for(auto& b:bufs) {
                    buffer_.append(b.p, b.size);
                }
            }
        } else {
            if(auto p = std::exchange(read_, nullptr)) {
                p->h_.resume();
            }
        }
    }

    void on_alloc(sstream* s,buffer_t& buf) {

    }

    void on_close() {
        close_ = 1;
        if(auto cb = std::exchange(cb_stop_, nullptr)) {
            cb->h_.resume();
        }
    }
    
    bool closed() {
        return close_ != 0;
    }

    int takebuf(std::string& s) {
        s.swap(buffer_);
        return s.size();
    }

    std::string buffer_;
    sstream stream_;
    ReadAwaiter* read_{nullptr};
    WriteAwaiter* write_{nullptr};
    StopAwaiter* cb_stop_{nullptr};
    int err_{1};
    int close_{0};
};

int main(int argc, char** argv) {
    
    sloop loop;
    co_ctx_t ctx(&loop);
    
    auto co_echo = [](co_ctx_t* ctx, UPtr<ssocket> sock) -> future_t<void> {
        co_session session(ctx, std::move(sock));
        for(;;) {
            auto buf = co_await session.co_read();
            if(buf.empty()) {
                break;
            }

            co_await session.co_write(buf);
        }
        co_await session.co_stop();
        co_return;
    };

    auto co_listen = [&](co_ctx_t* ctx) -> future_t<void> {

        co_tcpserver svr{ctx, addr_storage_t{8888}};
        std::list<future_t<void>> cons;

        for(;;) {
            auto sock = co_await svr.co_accept();
            if (!std::get<0>(sock)) {
                break;
            }

            cons.emplace_back(co_echo(ctx, std::move(std::get<0>(sock))));
        }

        co_await svr.co_stop();
        std::cout << "co final" << std::endl;
    };
    auto f = co_listen(&ctx);

    loop.run();
    return 0;
}

