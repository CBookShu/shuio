#include "shuio/shu_server.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include <cassert>

namespace shu {

	struct acceptor_complete_t : OVERLAPPED {
		enum { buffer_size = (sizeof(sockaddr) + 16) * 2 };

		std::unique_ptr<ssocket> sock_;
		char buffer_[buffer_size];

		acceptor_complete_t() {
			std::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
		}
	};

	struct sserver::sserver_t : std::enable_shared_from_this<sserver::sserver_t> {
		sloop* loop_;
		std::unique_ptr<ssocket> sock_;
		addr_storage_t addr_;
		sserver::func_newclient_t creator_;
		acceptor_complete_t accept_op[4];
		bool stop_;

		std::shared_ptr<sserver::sserver_t> holder_;

		sserver_t(sloop* loop, sserver::func_newclient_t&& creator, addr_storage_t addr) {
			loop_ = loop;
			stop_ = false;
			creator_ = std::forward<sserver::func_newclient_t>(creator);
			addr_ = addr;
		}
		~sserver_t() {
			
		}

		void start() {
			// 先创建 sock和对应的bind和listen
			sock_ = std::make_unique<ssocket>();
			sock_->init(addr_.udp == 1);
			sock_->reuse_addr(true);
			sock_->noblock(true);

			if (!addr_.udp) {
				if (!sock_->bind(addr_)) {
					return;
				}
				if (!sock_->listen()) {
					return;
				}
			}
			else {
				// TODO: UDP 直接进行read操作
			}

			// 绑定IOCP 并开始post accept
			navite_attach_iocp(loop_, sock_.get());
			navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
				run(entry);
			});

			for (auto& acceptor : accept_op) {
				if (!post_acceptor(&acceptor)) {
					stop();
					return;
				}
			}

			holder_ = shared_from_this();
			return ;
		}

		bool post_acceptor(acceptor_complete_t* acceptor) {
			acceptor->sock_ = std::make_unique<ssocket>();
			acceptor->sock_->init(addr_.udp);

			auto* client_sock = navite_cast_ssocket(acceptor->sock_.get());
			auto* server_sock = navite_cast_ssocket(sock_.get());
			DWORD bytes_read = 0;
			auto r = win32_extension_fns::AcceptEx(
				server_sock->s,
				client_sock->s,
				acceptor->buffer_, 0,
				sizeof(sockaddr) + 16, sizeof(sockaddr) + 16,
				&bytes_read, acceptor);

			if (!r) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					return false;
				}
			}
			return true;
		}

		void run(OVERLAPPED_ENTRY* entry) {
			auto* op = static_cast<struct acceptor_complete_t*>(entry->lpOverlapped);

			SOCKADDR_IN* ClientAddr = NULL;
			SOCKADDR_IN* LocalAddr = NULL;
			int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);

			// TODO: 这里可以优化将第一个包和accept一并获取到！
			// 我就喜欢这样的api，没有返回值！ 一定成功！
			win32_extension_fns::GetAcceptExSockaddrs(op->buffer_, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				(LPSOCKADDR*)&LocalAddr, &localLen,
				(LPSOCKADDR*)&ClientAddr, &remoteLen);
			
			addr_pair_t addr{};
			addr.remote.port = ntohs(ClientAddr->sin_port);
			addr.remote.ip.resize(64);
			inet_ntop(AF_INET, &ClientAddr->sin_addr, addr.remote.ip.data(), addr.remote.ip.size());
			addr.remote.udp = addr_.udp;

			addr.local.port = ntohs(LocalAddr->sin_port);
			addr.local.ip.resize(64);
			inet_ntop(AF_INET, &LocalAddr->sin_addr, addr.local.ip.data(), addr.local.ip.size());
			addr.local.udp = addr_.udp;

			socket_io_result_t res{ .err = 0 };
			creator_(res, std::move(op->sock_), addr);
			if (!post_acceptor(op)) {
				// error 
				socket_io_result_t res_err{ .err = 1, .naviteerr = s_last_error()};
				creator_(res_err, nullptr, {});
				stop();
			}
		}
	
		void stop() {
			if (std::exchange(stop_, true)) {
				return;
			}

			auto* navite_sock = navite_cast_ssocket(sock_.get());
			for (auto& acceptor : accept_op) {
				::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &acceptor);
			}
			sock_->close();
			auto self = shared_from_this();
			loop_->post([this, self] {
				this->holder_.reset();
			});
		}
	};

	sserver::sserver()
	{
	}

	sserver::sserver(sserver&& other) noexcept
	{
		s_.swap(other.s_);
	}

	sserver::~sserver()
	{
		if (auto sptr = s_.lock()) {

		}
	}

	void sserver::start(sloop* loop, func_newclient_t&& creator, addr_storage_t addr)
	{
		auto sptr = std::make_shared<sserver_t>(loop, std::forward<func_newclient_t>(creator), addr);
		s_ = sptr;
		loop->dispatch([sptr]() {
			sptr->start();
		});
	}

	void sserver::stop()
	{
		if (auto sptr = s_.lock()) {
			sptr->loop_->dispatch([sptr]() {
				sptr->stop();
			});
		}
	}

};