#pragma once
#include <cstdint>
#include <type_traits>
#include <cstring>
#include <iostream>
#include <concepts>
#include <array>
#include <any>

#include <functional>
#include <source_location>

namespace shu {

#define S_DISABLE_COPY(T)	\
	T(T&) = delete;\
	T& operator = (T&) = delete;\

	struct addr_storage_t {
		int port;
		std::array<char,64> ip;
		addr_storage_t(int port_, std::string_view ip_ = "0.0.0.0"):port(port_) {
			// TODO: assert(ip_.size() < ip.size());
			std::copy(ip_.begin(), ip_.end(), ip.begin());
			ip[ip_.size()] = 0;
		}
		addr_storage_t():port(0) {
			std::string_view ip_ = "0.0.0.0";
			std::copy(ip_.begin(), ip_.end(), ip.begin());
			ip[ip_.size()] = 0;
		}
	};

	struct addr_pair_t {
		addr_storage_t remote;
		addr_storage_t local;
	};

	typedef struct socket_io_result_t {
		int res;		// >0 write,read count;connect suc; <0 error; 0 eof or manual stop
		// win32: -WSAGetLastError
		// linux: -errno
	}socket_io_result;

	using func_t = std::function<void()>;

	// tool for variant
	template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
	template<class... Ts> overload(Ts...) -> overload<Ts...>;

	// exception check
	// TODO: C++23 add stacktrace
	void exception_check(
		bool con,
		std::string_view msg = {},
		std::source_location call = std::source_location::current()
	) noexcept;

	int s_last_error();

	// void* -> struct sockaddr_in
	void sockaddr_2_storage(void*, addr_storage_t*);
	bool storage_2_sockaddr(addr_storage_t*, void*);
};