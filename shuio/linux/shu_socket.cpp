
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

		void init(bool udp, bool v6) {
			shu::panic(fd == -1);
			int type = SOCK_STREAM;
			int protocol = IPPROTO_TCP;
			if (udp == 1) {
				type = SOCK_DGRAM;
				protocol = IPPROTO_UDP;
			}
			int family = AF_INET;
			if (v6) {
				family = AF_INET6;
			}
			fd = ::socket(family, type, protocol);
			shu::panic(fd != -1);
		}

		bool noblock(bool flag) {
			if (opt_.flags.noblock == flag) {
				return true;
			}
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
		int keepalive(bool enable,int delay) {
			if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable))) {
				return -s_last_error();
			}	  

			if (!enable) {
				return 1;
			}

			if (delay < 1) {
				return -1;
			}

			if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &delay, sizeof(delay))) {
				return -s_last_error();
			}

			int intvl = 1;  /* 1 second; same as default on Win32 */
			if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl))) {
				return -s_last_error();
			}

			int cnt = 10;  /* 10 retries; same as hardcoded on Win32 */
  			if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt))) {
				return -s_last_error();
			}

			return -s_last_error();
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

		int bind(struct sockaddr* addr, std::size_t len) {
			int r = ::bind(fd, addr, len);
			if (r == 0) {
				return 1;
			}
			return -s_last_error();
		}

		int listen() {
			int r = ::listen(fd, SOMAXCONN);
			if (0 == r) {
				return 1;
			}
			return -s_last_error();
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

		int shutdown(shutdown_type how) {
			int t = SHUT_RD;
			if (how == shutdown_type::shutdown_write)
				t = SHUT_WR;
			else if(how == shutdown_type::shutdown_both){
				t = SHUT_RDWR;
			}
			int r = ::shutdown(fd, t);
			if (0 == r) {
				return 1;
			}
			return -s_last_error();
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

	int ssocket::reuse_port(bool flag)
	{
        return ss_->reuse_port(flag);
	}

	int ssocket::keepalive(bool enable,int delay) {
		return ss_->keepalive(enable, delay);
	}

	int ssocket::nodelay(bool flag)
	{
		return ss_->nodelay(flag);
	}

	int ssocket::bind(void* addr, std::size_t len) {
		return ss_->bind((struct sockaddr*)addr, len);
	}

	int ssocket::listen() {
		return ss_->listen();
	}

	void ssocket::close() {
		return ss_->close();
	}

	bool ssocket::valid() {
		return ss_->valid();
	}

	int ssocket::shutdown(shutdown_type how) {
		return ss_->shutdown(how);
	}
}