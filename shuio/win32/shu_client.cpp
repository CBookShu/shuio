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
        bool close_;

        sclient_t(sloop* loop, sclient* owner, sclient_ctx&& callback, addr_storage_t addr)
        : loop_(loop),owner_(owner), cb_ctx_(std::forward<sclient_ctx>(callback)),
        addr_pair_{.remote = addr},stopping_(false),close_(false)
        {
            connector_ = {};
            shu::panic(!!cb_ctx_.evConn);
        }

        void post_to_close() {
            if (std::exchange(close_, true)) {
                return;
            }
            if (cb_ctx_.evClose) {
                loop_->post([f = std::move(cb_ctx_.evClose), owner = owner_](){
                    f(owner);
                });
            }
        }

        int start() {
            sock_ = std::make_unique<ssocket>();
            sock_->init(false, addr_pair_.remote.family() == AF_INET6);
            sock_->noblock(true);

            auto* navite_sock = navite_cast_ssocket(sock_.get());

            sockaddr_storage addr;
            shu::storage_2_sockaddr(&addr_pair_.local, &addr);
            int len = addr.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
            if (int err = sock_->bind(&addr, len); err <= 0) {
                socket_io_result res{ .res = err };
                cb_ctx_.evConn(res, nullptr, addr_pair_);
                return res.res;
            }
            if(0 == ::getsockname(navite_sock->s, (struct sockaddr*)&addr, &len)) {
                shu::sockaddr_2_storage(&addr, &addr_pair_.local);
            }

            navite_attach_iocp(loop_, sock_.get());
            // this 一定要比connector_ 活得久
            // 不把hold 放在lambda中，因为sock_绑定的这个Lambda生命周期太久了
            navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
                run(entry);
            });
            DWORD dwBytes = 0;

            shu::zero_mem(addr);
            len = addr.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
            shu::storage_2_sockaddr(&addr_pair_.remote, &addr);
            struct sockaddr* p = (struct sockaddr*)&addr;
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
                sock_->close();
            }
            post_to_close();
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