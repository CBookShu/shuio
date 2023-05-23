#pragma once
#include <cstdint>
#include <type_traits>
#include <cstring>
#include <iostream>
#include <concepts>

namespace shu {

#define S_DISABLE_COPY(T)	\
	T(T&) = delete;\
	T& operator = (T&) = delete;\
	T& operator = (T&&) = delete;\

#define S_FIX(msg)	static_assert(true, msg)

	template <typename T>
	requires requires {!std::is_pointer_v<T>;}
	static void s_copy(T&& d, T&& s) {
		d = std::forward<T&&>(s);
	}
	template <typename D, typename S>
	requires requires {std::is_pod_v<D>;std::is_pod_v<S>;sizeof(D)==sizeof(S);}
	static void s_copy(D&& d, S&& s) {
		std::memcpy(&d, &s, sizeof(d));
	}

	template <typename F>
	struct s_defer {
		F f;
		s_defer(auto&& cb):f(std::move(cb)) {}
		~s_defer() {
			f();
		}
	};

	template <typename T>
	static auto s_defer_make(T&& cb) -> s_defer<T>  {
		return s_defer<T>(std::forward<T&&>(cb));
	}

#define S_DEFER(exp)	s_defer_make([&](){exp});

	typedef struct addr_storage_t {
		int iptype;//0 tcp,1 udp
		int port;
		char ip[64];
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

	int s_last_error();
};