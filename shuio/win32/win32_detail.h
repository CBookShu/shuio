#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <ws2def.h>
#pragma comment(lib, "ws2_32.lib")

#include <variant>
#include <functional>

namespace shu {
	struct iocp_timer_t {};
	struct iocp_stop_t {};
	struct iocp_wake_t {};
	struct iocp_socket_t {
		std::function<void(OVERLAPPED_ENTRY*)> cb;
	};

	static_assert(sizeof(ULONG_PTR) == sizeof(void*));
	using iocp_task_union = std::variant<iocp_timer_t, iocp_stop_t, iocp_wake_t, iocp_socket_t>;

	const iocp_task_union tag_timer = iocp_task_union{ iocp_timer_t {} };
	const iocp_task_union tag_stop = iocp_task_union{ iocp_stop_t {} };
	const iocp_task_union tag_wake = iocp_task_union{ iocp_wake_t {} };

	typedef struct iocp_navite_t {
		HANDLE iocp;
	}iocp_navite_t;
	typedef struct fd_navite_t {
		SOCKET s;
		iocp_task_union tag = iocp_socket_t();
	}fd_navite_t;

	typedef BOOL(WINAPI* AcceptExPtr)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
	typedef BOOL(WINAPI* ConnectExPtr)(SOCKET, const struct sockaddr*, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);
	typedef void (WINAPI* GetAcceptExSockaddrsPtr)(PVOID, DWORD, DWORD, DWORD, LPSOCKADDR*, LPINT, LPSOCKADDR*, LPINT);

	struct win32_extension_fns {
		static AcceptExPtr AcceptEx;
		static ConnectExPtr ConnectEx;
		static GetAcceptExSockaddrsPtr GetAcceptExSockaddrs;

		// from libuv check lsp
		static int tcp_non_ifs_lsp_ipv4;
		static int tcp_non_ifs_lsp_ipv6;
	};

	class sloop;
	auto navite_cast_loop(sloop*) -> iocp_navite_t*;
	class ssocket;
	auto navite_cast_ssocket(ssocket*) -> fd_navite_t*;

	auto navite_attach_iocp(sloop*, ssocket*) -> bool;

	template <typename F>
	inline void navite_sock_setcallbak(ssocket* s, F&& f) {
		fd_navite_t* sock = navite_cast_ssocket(s);
		std::get_if<iocp_socket_t>(&sock->tag)->cb = std::forward<F>(f);
	}
};

