#include "shu_socket.h"
#include "win32_detail.h"
#include <cassert>

namespace {
	struct wsa_global_init {
		wsa_global_init() {
			WSADATA wsaData;
			(void)WSAStartup(MAKEWORD(2, 2), &wsaData);
		}
		~wsa_global_init() {
			WSACleanup();
		}
	}G_wsa_initor;
};

namespace shu {
	struct ssocket::ssocket_t : sock_navite_t {
		ssocket_opt opt;
	};


	ssocket::ssocket(ssocket_opt opt)
	{
		_ss = new ssocket_t{};
		_ss->s = INVALID_SOCKET;
		_ss->opt = opt;
	}

	ssocket::ssocket(ssocket&& other)  noexcept
	{
		_ss = std::exchange(other._ss, nullptr);
		_ss = other._ss;
		other._ss = nullptr;
	}

	ssocket::~ssocket()
	{
		if (_ss->s != INVALID_SOCKET) {
			::shutdown(_ss->s, SD_BOTH);
			::closesocket(_ss->s);
		}
		delete _ss;
	}

	auto ssocket::handle() -> ssocket_t*
	{
		return _ss;
	}

	auto ssocket::init(int iptype) -> bool
	{
		assert(_ss->s == INVALID_SOCKET);
		int type = SOCK_STREAM;
		int protocol = IPPROTO_TCP;
		if (iptype == 1) {
			type = SOCK_DGRAM;
			protocol = IPPROTO_UDP;
		}
		_ss->s = ::WSASocket(AF_INET, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
		assert(_ss->s != INVALID_SOCKET);
		
		if (_ss->s != INVALID_SOCKET) {
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
		auto r = ::ioctlsocket(_ss->s, FIONBIO, (&i));
		if (r == 0) {
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
		auto r = ::setsockopt(_ss->s, SOL_SOCKET, SO_REUSEADDR, (char*)(&i), sizeof(i));
		if (r == 0) {
			_ss->opt.flags.reuse_addr = flag;
			return true;
		}
		return false;
	}

	auto ssocket::reuse_port(bool) -> bool
	{
		return false;
	}

	auto ssocket::nodelay(bool flag) -> bool
	{
		if (_ss->opt.flags.nodelay == flag) {
			return true;
		}
		u_long i = flag;
		auto r = ::setsockopt(_ss->s, IPPROTO_TCP, TCP_NODELAY, (char*)(&i), sizeof(i));
		if (r == 0) {
			_ss->opt.flags.nodelay = flag;
			return true;
		}
		return false;
	}


};