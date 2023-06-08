#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#pragma comment(lib, "ws2_32.lib")

namespace shu {
	struct socket_user_op;
	typedef struct iocp_navite_t {
		HANDLE iocp;
	}iocp_navite_t;
	typedef struct sock_navite_t {
		SOCKET s;
	}sock_navite_t;

	enum IOCP_OP_TYPE{
		NONE_TYPE = 0,
		SOCKET_TYPE = 1,
		TASK_TYPE = 2,
		TIMER_TYPE = 3,
		STOP_TYPE = 4,
	};

	struct iocp_sock_callback {
		virtual ~iocp_sock_callback() {}
		virtual void run(OVERLAPPED_ENTRY* entry) noexcept = 0;
		virtual void destroy() noexcept { delete this; }
	};

	struct socket_user_op : public OVERLAPPED {
		iocp_sock_callback* cb;
	};

	class sloop;
	auto navite_cast_loop(sloop*) -> iocp_navite_t*;
	class ssocket;
	auto navite_cast_ssocket(ssocket*) -> sock_navite_t*;

	auto navite_attach_iocp(sloop*, ssocket*, IOCP_OP_TYPE) -> bool;
};

