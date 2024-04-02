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
        std::unique_ptr<ssocket> sock_;
        func_connect_t callback_;
        addr_storage_t remote_addr_;
        OVERLAPPED connector_;

        std::shared_ptr<sclient_t> hold_;
        bool stopping;

        sclient_t(sloop* loop, func_connect_t&& callback, addr_storage_t addr) {
            loop_ = loop;
            callback_ = callback;
            remote_addr_ = addr;
            connector_ = {};
            stopping = false;
        }

        void start() {
            sock_ = std::make_unique<ssocket>();
            sock_->init(remote_addr_.udp);
            sock_->noblock(true);

            auto* navite_sock = navite_cast_ssocket(sock_.get());

            navite_attach_iocp(loop_, sock_.get());
            // this 一定要比connector_ 活得久
            // 不把hold 放在lambda中，因为sock_绑定的这个Lambda生命周期太久了
            navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
                run(entry);
            });

            addr_storage_t bind_addr;
            bind_addr.port = 0;
            bind_addr.udp = false;
            if (!sock_->bind(bind_addr)) {
                auto err = s_last_error();
                socket_io_result res{ .bytes = 0, .err = 1, .naviteerr = err };
                callback_(res, nullptr, addr_pair_t{ .remote = remote_addr_, .local = {} });
                return;
            }

            sockaddr_in addr{};
            int len = sizeof(addr);
            storage_2_sockaddr(&remote_addr_, &addr);

            DWORD dwBytes = 0;
            BOOL r = win32_extension_fns::ConnectEx(navite_sock->s, (struct sockaddr*)&addr, len, nullptr, 0, &dwBytes, &connector_);
            if (!r) {
                auto err = s_last_error();
                if (err != ERROR_IO_PENDING) {
                    socket_io_result res{ .bytes = 0, .err = 1, .naviteerr = err };
                    callback_(res, nullptr, addr_pair_t{ .remote = remote_addr_, .local = {} });
                    return;
                }
            }

            // 保住自己
            hold_ = shared_from_this();
        }

        void run(OVERLAPPED_ENTRY* entry) {
            if (entry->dwNumberOfBytesTransferred != 0) {
                socket_io_result res{ .bytes = 0, .err = 1, .naviteerr = s_last_error() };
                callback_(res, nullptr, addr_pair_t{ .remote = remote_addr_, .local = {} });
            }
            else {
                socket_io_result res{ .err = 0 };
                auto* navite_sock = navite_cast_ssocket(sock_.get());
                addr_pair_t addr_pair{ .remote = remote_addr_};
                struct sockaddr_in addr;
                int len = sizeof(addr);
                if (0 == getsockname(navite_sock->s, (struct sockaddr*)&addr, &len)) {
                    sockaddr_2_storage(&addr, &addr_pair.local);
                }
                callback_(res, std::move(sock_), addr_pair);
            }

            // 释放自己
            hold_.reset();
        }
    
        void stop() {
            if (std::exchange(stopping, true)) {
                return;
            }

            auto* navite_sock = navite_cast_ssocket(sock_.get());
            ::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &connector_);
        }
    };

    sclient::sclient()
    {}
    sclient::sclient(sclient&& other) noexcept
    {
        s_.swap(other.s_);
    }
    sclient::~sclient()
    {
        auto sptr = s_.lock();
        if (sptr) {
            // TODO: 其实sclient 也是可以让它自生自灭的
            //shu::exception_check(!sptr, "client_t must close before ~sclient");
        }
    }
    void sclient::start(sloop* loop, addr_storage_t saddr, func_connect_t&& cb)
    {
        auto sptr = std::make_shared<sclient_t>(loop, std::forward<func_connect_t>(cb), saddr);
        s_ = sptr;
        loop->dispatch([sptr]() {
            sptr->start();
        });
    }
    void sclient::stop()
    {
        if (auto sptr = s_.lock()) {
            sptr->loop_->dispatch([sptr]() {
                sptr->stop();
            });
        }
    }
};