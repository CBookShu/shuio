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

		void init(bool udp) {
			shu::exception_check(s == INVALID_SOCKET);
			int type = SOCK_STREAM;
			int protocol = IPPROTO_TCP;
			if (udp) {
				type = SOCK_DGRAM;
				protocol = IPPROTO_UDP;
			}
			s = ::WSASocket(AF_INET, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
			shu::exception_check(s != INVALID_SOCKET);
			opt_.flags.udp = udp ? 1 : 0;
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

		bool bind(addr_storage_t addr) {
			sockaddr_in addr_in{};
			addr_in.sin_family = AF_INET;
			addr_in.sin_port = htons(addr.port);
			if (addr.ip.empty()) {
				addr_in.sin_addr = in4addr_any;
			}
			else {
				auto r = ::inet_pton(AF_INET, addr.ip.data(), &addr_in.sin_addr);
				if (r != 1) {
					return false;
				}
			}

			auto r = ::bind(s, (struct sockaddr*)&addr_in, sizeof(addr_in));
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

	void ssocket::init(bool udp)
	{
		return ss_->init(udp);
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

	auto ssocket::bind(addr_storage_t addr) -> bool
	{
		return ss_->bind(addr);
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