
#include "shuio/shu_socket.h"
#include "linux_detail.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>

#include <utility>

namespace shu {

    struct ssocket::ssocket_t : fd_navite_t {
        ssocket_opt opt_;
		ssocket_t() {
			fd = -1;
			opt_ = {};
		}
		~ssocket_t() {
			close();
		}

		void init(bool udp) {
			shu::exception_check(fd == -1);
			int type = SOCK_STREAM;
			int protocol = IPPROTO_TCP;
			if (udp == 1) {
				type = SOCK_DGRAM;
				protocol = IPPROTO_UDP;
			}
			
			fd = ::socket(AF_INET, type, protocol);
			shu::exception_check(fd != -1);
			opt_.flags.udp = udp;
		}

		bool noblock(bool flag) {
			if (opt_.flags.noblock == flag) {
				return true;
			}
			u_long i = flag;
			int status = ::fcntl(fd, F_GETFL);
			
			if(flag) {
				if(status & O_NONBLOCK) {
					opt_.flags.noblock = flag;
					return true;
				}
				status = status | O_NONBLOCK;
			} else {
				if (!(status & O_NONBLOCK)) {
					opt_.flags.noblock = flag;
					return true;
				}
				status = status & ~O_NONBLOCK;
			}
			if(0 == ::fcntl(fd, F_SETFL, status)) {
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
			auto r = ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)(&i), sizeof(i));
			if (r == 0) {
				opt_.flags.reuse_addr = flag;
				return true;
			}
			return false;
		}

		bool reuse_port(bool flag)
		{
			if (opt_.flags.reuse_port == flag) {
				return true;
			}
			u_long i = flag;
			auto r = ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char*)(&i), sizeof(i));
			if (r == 0) {
				opt_.flags.reuse_port = flag;
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
			auto r = ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)(&i), sizeof(i));
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
				addr_in.sin_addr.s_addr = INADDR_ANY;
			}
			else {
				auto r = ::inet_pton(AF_INET, addr.ip.c_str(), &addr_in.sin_addr);
				if (r != 1) {
					return false;
				}
			}

			auto r = ::bind(fd, (struct sockaddr*)&addr_in, sizeof(addr_in));
			return r == 0;
		}

		bool listen() {
			return ::listen(fd, SOMAXCONN) == 0;
		}

		void close() {
			if (fd != -1) {
				::close(fd);
				fd = -1;
			}
		}

		bool valid() {
			return fd != -1;
		}

		void shutdown(shutdown_type how) {
			int t = SHUT_RD;
			if (how == shutdown_write)
				t = SHUT_WR;
			else if(how == shutdown_both){
				t = SHUT_RDWR;
			}
			::shutdown(fd, t);
		}
    };

    ssocket::ssocket() {
        ss_ = new ssocket_t{};
    }
    ssocket::ssocket(ssocket&& other)  noexcept {
        ss_ = std::exchange(other.ss_, nullptr);
    }
    ssocket::~ssocket() {
        if(ss_) {
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

	auto ssocket::reuse_port(bool flag) -> bool
	{
        return ss_->reuse_port(flag);
	}

	auto ssocket::nodelay(bool flag) -> bool
	{
		return ss_->nodelay(flag);
	}

	bool ssocket::bind(addr_storage_t addr) {
		return ss_->bind(addr);
	}

	bool ssocket::listen() {
		return ss_->listen();
	}

	void ssocket::close() {
		return ss_->close();
	}

	bool ssocket::valid() {
		return ss_->valid();
	}

	void ssocket::shutdown(shutdown_type how) {
		return ss_->shutdown(how);
	}
}