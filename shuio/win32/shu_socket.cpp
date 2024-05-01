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
			shu::panic(s == INVALID_SOCKET);
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
			s = ::WSASocket(family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
			shu::panic(s != INVALID_SOCKET);

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
					shu::panic(false, "HANDLE_SYNC_BYPASS_IOCP");
				}
			}
		}

		int noblock(bool flag) {
			if (opt_.flags.noblock == flag) {
				return 1;
			}
			u_long i = flag;
			auto r = ::ioctlsocket(s, FIONBIO, (&i));
			if (r == 0) {
				opt_.flags.noblock = flag;
				return 1;
			}
			return -s_last_error();
		}

		int reuse_addr(bool flag)
		{
			if (opt_.flags.reuse_addr == flag) {
				return 1;
			}
			u_long i = flag;
			auto r = ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)(&i), sizeof(i));
			if (r == 0) {
				opt_.flags.reuse_addr = flag;
				return 1;
			}
			return -s_last_error();
		}

		int keepalive(bool enable,int delay) {
			if (setsockopt(s,
						SOL_SOCKET,
						SO_KEEPALIVE,
						(const char*)&enable,
						sizeof enable) == -1) {
				return -s_last_error();
			}

			if(!enable) {
				return 1;
			}

			if(delay < 1) {
				return -1;
			}

			if (setsockopt(s,
						IPPROTO_TCP,
						TCP_KEEPALIVE,
						(const char*)&delay,
						sizeof delay) == -1) {
				return -s_last_error();
			}
			return 1;
		}

		int nodelay(bool flag)
		{
			if (opt_.flags.nodelay == flag) {
				return 1;
			}
			u_long i = flag;
			auto r = ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)(&i), sizeof(i));
			if (r == 0) {
				opt_.flags.nodelay = flag;
				return 1;
			}
			return -s_last_error();
		}

		int bind(struct sockaddr* addr, std::size_t len) {
			if(::bind(s, addr, len) != SOCKET_ERROR) {
				return 1;
			}
			return -s_last_error();
		}

		int listen() {
			if(::listen(s, SOMAXCONN) != SOCKET_ERROR) {
				return 1;
			}
			return -s_last_error();
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

		int shutdown(shutdown_type how) {
			int t = SD_RECEIVE;
			if (how == shutdown_type::shutdown_write)
				t = SD_SEND;
			else if(how == shutdown_type::shutdown_both){
				t = SD_BOTH;
			}
			if(::shutdown(s, t) != SOCKET_ERROR) {
				return 1;
			}
			return -s_last_error();
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

	int ssocket::noblock(bool flag)
	{
		return ss_->noblock(flag);
	}

	int ssocket::reuse_addr(bool flag)
	{
		return ss_->reuse_addr(flag);
	}

	int ssocket::reuse_port(bool)
	{
		return 0;
	}

	int ssocket::keepalive(bool enable,int delay) {
		return ss_->keepalive(enable, delay);
	}

	int ssocket::nodelay(bool flag)
	{
		return ss_->nodelay(flag);
	}

	int ssocket::bind(void* addr, std::size_t len)
	{
		return ss_->bind((struct sockaddr*)addr, len);
	}

	int ssocket::listen()
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

	int ssocket::shutdown(shutdown_type how)
	{
		return ss_->shutdown(how);
	}

};