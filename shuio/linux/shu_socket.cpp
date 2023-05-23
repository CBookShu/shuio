
#include "shuio/shu_socket.h"
#include "linux_detail.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>

#include <utility>

namespace shu {

    struct ssocket::ssocket_t : sock_navite_t {
        ssocket_opt opt;
    };

    ssocket::ssocket(ssocket_opt opt) {
        _ss = new ssocket_t{};
        _ss->fd = -1;
        _ss->opt = opt;
    }
    ssocket::ssocket(ssocket&& other)  noexcept {
        _ss = std::exchange(other._ss, nullptr);
    }
    ssocket::~ssocket() {
        if(!_ss) {
            return;
        }
        if(_ss->fd != -1) {
            ::shutdown(_ss->fd, SHUT_RDWR);
            ::close(_ss->fd);
        }
        delete _ss;
    }

    auto ssocket::handle() -> ssocket_t*
	{
		return _ss;
	}

    auto ssocket::init(int iptype) -> bool
	{
		assert(_ss->fd == -1);
		int type = SOCK_STREAM;
		int protocol = IPPROTO_TCP;
		if (iptype == 1) {
			type = SOCK_DGRAM;
			protocol = IPPROTO_UDP;
		}
        
		_ss->fd = ::socket(AF_INET, type, protocol);
		assert(_ss->fd != -1);
		
		if (_ss->fd != -1) {
			_ss->opt.flags.udp = iptype;
			return true;
		}
		return false;
	}

    auto ssocket::option() -> const ssocket_opt*
	{
		return &_ss->opt;
	}

	auto ssocket::noblock(bool flag) -> bool
	{
		if (_ss->opt.flags.noblock == flag) {
			return true;
		}
		u_long i = flag;
        int status = ::fcntl(_ss->fd, F_GETFL);
        
        if(flag) {
            if(status & O_NONBLOCK) {
                _ss->opt.flags.noblock = flag;
			    return true;
            }
            status = status | O_NONBLOCK;
        } else {
            if (!(status & O_NONBLOCK)) {
                _ss->opt.flags.noblock = flag;
			    return true;
            }
            status = status & ~O_NONBLOCK;
        }
        if(0 == ::fcntl(_ss->fd, F_SETFL, status)) {
            _ss->opt.flags.noblock = flag;
			return true;
        }
        return false;
	}

	auto ssocket::reuse_addr(bool flag) -> bool
	{
		if (_ss->opt.flags.reuse_addr == flag) {
			return true;
		}
		u_long i = flag;
		auto r = ::setsockopt(_ss->fd, SOL_SOCKET, SO_REUSEADDR, (char*)(&i), sizeof(i));
		if (r == 0) {
			_ss->opt.flags.reuse_addr = flag;
			return true;
		}
		return false;
	}

	auto ssocket::reuse_port(bool flag) -> bool
	{
        if (_ss->opt.flags.reuse_port == flag) {
			return true;
		}
		u_long i = flag;
		auto r = ::setsockopt(_ss->fd, SOL_SOCKET, SO_REUSEPORT, (char*)(&i), sizeof(i));
		if (r == 0) {
			_ss->opt.flags.reuse_port = flag;
			return true;
		}
		return false;
	}

	auto ssocket::nodelay(bool flag) -> bool
	{
		if (_ss->opt.flags.nodelay == flag) {
			return true;
		}
		u_long i = flag;
		auto r = ::setsockopt(_ss->fd, IPPROTO_TCP, TCP_NODELAY, (char*)(&i), sizeof(i));
		if (r == 0) {
			_ss->opt.flags.nodelay = flag;
			return true;
		}
		return false;
	}
}