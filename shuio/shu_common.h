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
		std::uint16_t port;
		std::uint16_t family;
		std::array<char,64> ip;
		// family_ = 2 默认是 IPV4, AF_INET
		addr_storage_t(int port_, std::string_view ip_ = "0.0.0.0", int family_ = 2)
		:port(port_), family(family_)
		{
			// TODO: assert(ip_.size() < ip.size());
			std::copy(ip_.begin(), ip_.end(), ip.begin());
			ip[ip_.size()] = 0;
		}
		addr_storage_t():port(0),family(0) {
			std::string_view ip_ = "0.0.0.0";
			std::copy(ip_.begin(), ip_.end(), ip.begin());
			ip[ip_.size()] = 0;
		}
	};

	template <typename T>
	using UPtr = std::unique_ptr<T>;

	template <typename T>
	using SPtr = std::shared_ptr<T>;

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
	void panic(
		bool con,
		std::string_view msg = {},
		std::source_location call = std::source_location::current()
	) noexcept;

	int s_last_error();

	// void* -> struct sockaddr_in
	void sockaddr_2_storage(void*, addr_storage_t*);
	bool storage_2_sockaddr(addr_storage_t*, void*);

	// common user data begin
	template <typename T>
	concept c_has_any_userdata = requires (T t) {
		{t.get_ud()}->std::same_as<std::any*>;
	};

	template <typename T, typename O>
	requires c_has_any_userdata<O>
	decltype(auto) get_user_data(O&& o) {
		std::any* a = std::forward<O>(o).get_ud();
		return std::any_cast<T>(a);
	}

	template <typename T, typename O, typename...Args>
	requires c_has_any_userdata<O>
	decltype(auto) set_user_data(O&& o, Args&&...args) {
		std::any* a = std::forward<O>(o).get_ud();
		*a = std::make_any<T>(std::forward<Args>(args)...);
		return std::any_cast<T>(a);
	}
	// common user end

	template <typename T>
		requires(std::is_trivial_v<T>)
	void zero_mem(T& t) {
		std::memset(&t, 0, sizeof(T));
	}

	template <typename T, std::size_t N>
		requires(std::is_trivial_v<T>)
	void zero_mem(T(&t)[N]) {
		std::memset(t, 0, sizeof(T) * N);
	}
};