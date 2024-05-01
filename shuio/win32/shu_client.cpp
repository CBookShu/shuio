#include "shuio/shu_client.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "win32_detail.h"

#include <thread>
#include <future>
#include <cassert>
#include <memory>

namespace shu {

    struct sclient::sclient_t : public std::enable_shared_from_this<sclient::sclient_t> {
        sloop* loop_;
        sclient* owner_;
        std::unique_ptr<ssocket> sock_;
        sclient_ctx cb_ctx_;
        addr_pair_t addr_pair_;
        OVERLAPPED connector_;
        bool stopping_;

        sclient_t(sloop* loop, sclient* owner, sclient_ctx&& callback, addr_storage_t addr)
        : loop_(loop),owner_(owner), cb_ctx_(std::forward<sclient_ctx>(callback)),
        addr_pair_{.remote = addr},stopping_(false)
        {
            connector_ = {};
            shu::panic(!!cb_ctx_.evConn);
        }

        void post_to_close() {
            if (cb_ctx_.evClose) {
                loop_->post([f = std::move(cb_ctx_.evClose), owner = owner_](){
                    f(owner);
                });
            }
        }

        int start() {
            addrinfo hints { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
            addrinfo *server_info {nullptr};
            auto service = std::to_string(addr_pair_.remote.port);
            std::string_view ip(addr_pair_.remote.ip.data());

            int rv = getaddrinfo(ip.data(), service.c_str(), &hints, &server_info);
            if(rv != 0) {
                socket_io_result res{ .res = -s_last_error() };
                cb_ctx_.evConn(res, nullptr, addr_pair_);
                return res.res;
            }
            auto f_deleter = [](addrinfo* p){ freeaddrinfo(p);};
            std::unique_ptr<addrinfo, void(*)(addrinfo*)> ptr(server_info, f_deleter);
            int last_error = 0;
            sockaddr_storage addr_remote;
            shu::zero_mem(addr_remote);
            int len = sizeof(addr_remote);
            for (auto p = server_info; p != nullptr; p = p->ai_next) {
                shu::panic(len >= p->ai_addrlen);
                std::memcpy(&addr_remote, p->ai_addr, p->ai_addrlen);
                len = p->ai_addrlen;

                addr_pair_.remote.family = p->ai_family;
                break;
            }

            sock_ = std::make_unique<ssocket>();
            sock_->init(false, addr_pair_.remote.family ==AF_INET6);
            sock_->noblock(true);

            auto* navite_sock = navite_cast_ssocket(sock_.get());

            addr_storage_t addr_bind;
            addr_bind.family = addr_remote.ss_family;
            addr_bind.port = 0;
            if (addr_remote.ss_family == AF_INET) {
                sockaddr_in addr4;
                shu::storage_2_sockaddr(&addr_bind, &addr4); 
                if (!sock_->bind(&addr4, sizeof(addr4))) {
                    socket_io_result res{ .res = -s_last_error() };
                    cb_ctx_.evConn(res, nullptr, addr_pair_);
                    return res.res;
                }
                int len = sizeof(addr4);
                if(0 == ::getsockname(navite_sock->s, (struct sockaddr*)&addr4, &len)) {
                    shu::sockaddr_2_storage(&addr4, &addr_pair_.local);
                }
            } else if (addr_remote.ss_family == AF_INET6) {
                sockaddr_in6 addr6;
                shu::storage_2_sockaddr(&addr_bind, &addr6); 
                if (!sock_->bind(&addr6, sizeof(addr6))) {
                    socket_io_result res{ .res = -s_last_error() };
                    cb_ctx_.evConn(res, nullptr, addr_pair_);
                    return res.res;
                }
                int len = sizeof(addr6);
                if(0 == ::getsockname(navite_sock->s, (struct sockaddr*)&addr6, &len)) {
                    shu::sockaddr_2_storage(&addr6, &addr_pair_.local);
                }
            } else {
                // panic ?
                socket_io_result res{ .res = -1 };
                cb_ctx_.evConn(res, nullptr, addr_pair_);
                return res.res;
            }

            navite_attach_iocp(loop_, sock_.get());
            // this 一定要比connector_ 活得久
            // 不把hold 放在lambda中，因为sock_绑定的这个Lambda生命周期太久了
            navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
                run(entry);
            });
            DWORD dwBytes = 0;
            struct sockaddr* p = (struct sockaddr*)&addr_remote;
            BOOL r = win32_extension_fns::ConnectEx(navite_sock->s, p, len, nullptr, 0, &dwBytes, &connector_);
            if (!r) {
                auto err = s_last_error();
                if (err != ERROR_IO_PENDING) {
                    socket_io_result res{ .res = -err };
                    cb_ctx_.evConn(res, nullptr, addr_pair_);
                    return res.res;
                }
            }
            return true;
        }

        void run(OVERLAPPED_ENTRY* entry) {
            if (entry->dwNumberOfBytesTransferred != 0) {
                // 删除已经创建的sock，证明调用已经完成了
                sock_.reset();
                socket_io_result res{ .res = -s_last_error() };
                cb_ctx_.evConn(res, nullptr, addr_pair_);
            }
            else {
                socket_io_result res{ .res = 1 };
                auto* navite_sock = navite_cast_ssocket(sock_.get());
                cb_ctx_.evConn(res, sock_.release(), addr_pair_);
            }

            if(stopping_) {
                post_to_close();
            }
        }
    
        void stop() {
            if (std::exchange(stopping_, true)) {
                return;
            }

            if(sock_) {
                // 需要关闭调用
                auto* navite_sock = navite_cast_ssocket(sock_.get());
                ::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &connector_);
            } else {
                // 要么是start 报错，要么是已经完成了
                post_to_close();
            }
        }
    };

    sclient::sclient():s_(nullptr)
    {}
    sclient::sclient(sclient&& other) noexcept
    {
        s_ = std::exchange(other.s_, nullptr);
    }
    sclient::~sclient()
    {
        if (s_) {
            delete s_;
        }
    }
    int sclient::start(sloop* loop, addr_storage_t saddr, sclient_ctx&& ctx)
    {
        shu::panic(!s_);

        s_ = new sclient_t(loop, this, std::forward<sclient_ctx>(ctx), saddr);
        int r = s_->start();
        return r;
    }

    auto sclient::loop() -> sloop* {
        shu::panic(s_);
        return s_->loop_;
    }

    void sclient::stop()
    {
        shu::panic(s_);
        s_->stop();
    }
};