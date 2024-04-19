#pragma once
#include <cstdint>
#include <type_traits>
#include <cstring>
#include <iostream>
#include <concepts>

#include <functional>
#include <source_location>

namespace shu {

#define S_DISABLE_COPY(T)	\
	T(T&) = delete;\
	T& operator = (T&) = delete;\

	template <typename F>
	struct s_defer {
		F f;
		s_defer(F&& cb):f(std::forward<F>(cb)) {}
		~s_defer() {
			f();
		}
	};

	template <typename T>
	static auto s_defer_make(T&& cb) -> s_defer<T>  {
		return s_defer<T>(std::forward<T&&>(cb));
	}

#define S_DEFER(exp)	s_defer_make([&](){exp;});

	typedef struct addr_storage_t {
		bool udp;//0 tcp,1 udp
		int port;
		std::string ip;
	}addr_storage_t;

	typedef struct addr_pair_t {
		addr_storage_t remote;
		addr_storage_t local;
	}addr_pair_t;

	typedef struct socket_io_result_t {
		std::uint32_t bytes;		// 本次操作的bytes
		std::int32_t err;		// 本次操作是否有错误0 无错误
		std::int32_t naviteerr;	// win: wsagetlasterror, unix:errno
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