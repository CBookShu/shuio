#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#pragma comment(lib, "ws2_32.lib")

namespace shu {
	typedef struct iocp_navite_t {
		HANDLE iocp;
	}iocp_navite_t;
	typedef struct sock_navite_t {
		SOCKET s;
	}sock_navite_t;

	struct iocp_sock_callback {
		virtual ~iocp_sock_callback() {}
		virtual void run(OVERLAPPED_ENTRY* entry) noexcept = 0;
		virtual void destroy() noexcept { delete this; }
	};

	class sloop;
	auto navite_cast_loop(sloop*) -> iocp_navite_t*;
	class ssocket;
	auto navite_cast_ssocket(ssocket*) -> sock_navite_t*;

	auto navite_attach_iocp(sloop*, ssocket*, void*) -> bool;
};

