#include "shuio/shu_socket.h"
#include "win32_detail.h"
#include <cassert>

namespace shu {
	struct ssocket::ssocket_t : fd_navite_t {
		ssocket_opt opt_;

		ssocket_t() {
			s = INVALID_SOCKET;
			opt_ = {};
		}
		~ssocket_t() {
			if (s != INVALID_SOCKET) {
				closesocket(s);
			}
		}

		void init(bool udp, bool v6) {
			shu::exception_check(s == INVALID_SOCKET);
			int type = SOCK_STREAM;
			int protocol = IPPROTO_TCP;
			if (udp) {
				type = SOCK_DGRAM;
				protocol = IPPROTO_UDP;
			}
			int family = AF_INET;
			if (v6) {
				family = AF_INET6;
			}
			s = ::WSASocket(AF_INET, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
			shu::exception_check(s != INVALID_SOCKET);

			bool non_ifs_lsp;
			if (family == AF_INET6) {
				non_ifs_lsp = win32_extension_fns::tcp_non_ifs_lsp_ipv6;
			} else {
				non_ifs_lsp = win32_extension_fns::tcp_non_ifs_lsp_ipv4;
			}
			if(!non_ifs_lsp) {
				UCHAR sfcnm_flags =
					FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS;
				if (!SetFileCompletionNotificationModes((HANDLE) s, sfcnm_flags)) {
					shu::exception_check(false, "HANDLE_SYNC_BYPASS_IOCP");
				}
			}
		}

		bool noblock(bool flag) {
			if (opt_.flags.noblock == flag) {
				return true;
			}
			u_long i = flag;
			auto r = ::ioctlsocket(s, FIONBIO, (&i));
			if (r == 0) {
				opt_.flags.noblock = flag;
				return true;
			}
			return false;
		}

		bool reuse_addr(bool flag)
		{
			if (opt_.flags.reuse_addr == flag) {
				return true;
			}
			u_long i = flag;
			auto r = ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)(&i), sizeof(i));
			if (r == 0) {
				opt_.flags.reuse_addr = flag;
				return true;
			}
			return false;
		}

		bool nodelay(bool flag)
		{
			if (opt_.flags.nodelay == flag) {
				return true;
			}
			u_long i = flag;
			auto r = ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)(&i), sizeof(i));
			if (r == 0) {
				opt_.flags.nodelay = flag;
				return true;
			}
			return false;
		}

		bool bind(struct sockaddr* addr, std::size_t len) {
			auto r = ::bind(s, addr, len);
			return r != SOCKET_ERROR;
		}

		bool listen() {
			return ::listen(s, SOMAXCONN) != SOCKET_ERROR;
		}

		void close() {
			if (s != INVALID_SOCKET) {
				closesocket(s);
				s = INVALID_SOCKET;
			}
		}

		bool valid() {
			return s != INVALID_SOCKET;
		}

		void shutdown(shutdown_type how) {
			int t = SD_RECEIVE;
			if (how == shutdown_type::shutdown_write)
				t = SD_SEND;
			else if(how == shutdown_type::shutdown_both){
				t = SD_BOTH;
			}
			::shutdown(s, t);
		}
	};


	ssocket::ssocket()
	{
		ss_ = new ssocket_t{};
	}

	ssocket::ssocket(ssocket&& other)  noexcept
	{
		ss_ = std::exchange(other.ss_, nullptr);
		ss_ = other.ss_;
		other.ss_ = nullptr;
	}

	ssocket::~ssocket()
	{
		if (ss_) {
			delete ss_;
		}
	}

	auto ssocket::handle() -> ssocket_t*
	{
		return ss_;
	}

	void ssocket::init(bool udp, bool v6)
	{
		return ss_->init(udp, v6);
	}

	auto ssocket::option() -> const ssocket_opt*
	{
		return &ss_->opt_;
	}

	auto ssocket::noblock(bool flag) -> bool
	{
		return ss_->noblock(flag);
	}

	auto ssocket::reuse_addr(bool flag) -> bool
	{
		return ss_->reuse_addr(flag);
	}

	auto ssocket::reuse_port(bool) -> bool
	{
		return false;
	}

	auto ssocket::nodelay(bool flag) -> bool
	{
		return ss_->nodelay(flag);
	}

	auto ssocket::bind(void* addr, std::size_t len) -> bool
	{
		return ss_->bind((struct sockaddr*)addr, len);
	}

	auto ssocket::listen() -> bool
	{
		return ss_->listen();
	}

	void ssocket::close()
	{
		return ss_->close();
	}

	bool ssocket::valid()
	{
		return ss_->valid();
	}

	void ssocket::shutdown(shutdown_type how)
	{
		return ss_->shutdown(how);
	}

};