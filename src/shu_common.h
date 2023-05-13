#pragma once
#include <cstdint>

namespace shu {

#define S_DISABLE_COPY(T)	\
	T(T&) = delete;\
	T& operator = (T&) = delete;\
	T& operator = (T&&) = delete;\

#define S_FIX(msg)	static_assert(true, msg)

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
		std::uint32_t bytes;		// ���β�����bytes
		std::int32_t err;		// ���β����Ƿ��д���0 �޴���
		std::int32_t naviteerr;	// win: wsagetlasterror, unix:errno
	}socket_io_result;

	int s_last_error();
};