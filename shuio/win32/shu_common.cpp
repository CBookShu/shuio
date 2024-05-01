#include "shuio/shu_common.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <filesystem>
#include <format>

namespace shu {
	void panic(bool con, std::string_view msg, std::source_location call) noexcept
	{
		if (!con) [[unlikely]] {
			auto path = std::filesystem::path(call.file_name());

			if (msg.empty()) {
				std::cerr << std::format("Exception @file:{} @line:{} @func:{}",
					path.filename().string(),
					call.line(),
					call.function_name());
			}
			else {
				std::cerr << std::format("Exception @msg:{} @file:{} @line:{} @func:{}",
					msg,
					path.filename().string(),
					call.line(),
					call.function_name());
			}
			std::cerr << std::endl;

			// C++23 std::unreachable()
			std::terminate();
		}
	}
	int s_last_error()
	{
		// TODO: need atomic op?
		int e = ::WSAGetLastError();
		::WSASetLastError(0);
		return e;
	}

	void sockaddr_2_storage(void* p, addr_storage_t* storage) {
		auto* addr_common = static_cast<sockaddr_storage*>(p);
		if (addr_common->ss_family == AF_INET) {
			auto* addr = static_cast<struct sockaddr_in*>(p);
			storage->port = ntohs(addr->sin_port);
			std::string_view sip(storage->ip.data());
			::inet_ntop(storage->family, addr, storage->ip.data(), storage->ip.size());
		} else if (addr_common->ss_family == AF_INET6) {
			auto* addr = static_cast<struct sockaddr_in6*>(p);
			storage->port = ntohs(addr->sin6_port);
			std::string_view sip(storage->ip.data());
			::inet_ntop(storage->family, addr, storage->ip.data(), storage->ip.size());
		} else {
			shu::panic(false, std::string("error family:") + std::to_string(addr_common->ss_family));
		}
	}

	bool storage_2_sockaddr(addr_storage_t* storage, void* p) {
		if(storage->family == AF_INET) {
			auto* addr = static_cast<struct sockaddr_in*>(p);
			addr->sin_family = storage->family;
			addr->sin_port =  htons(storage->port);
			return ::inet_pton(storage->family, storage->ip.data(), &(addr->sin_addr)) == 1;
		} else if (storage->family == AF_INET6) {
			auto* addr = static_cast<struct sockaddr_in6*>(p);
			addr->sin6_family = storage->family;
			addr->sin6_port =  htons(storage->port);
			return ::inet_pton(storage->family, storage->ip.data(), &(addr->sin6_addr)) == 1;
		} else {
			shu::panic(false, std::string("error family:") + std::to_string(storage->family));
		}
	}
}