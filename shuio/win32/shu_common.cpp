#include "shuio/shu_common.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <filesystem>
#include <format>

namespace shu {
	void exception_check(bool con, std::string_view msg, std::source_location call) noexcept
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
		auto* addr = static_cast<struct sockaddr_in*>(p);
		storage->port = ntohl(addr->sin_port);
		std::string_view sip(storage->ip.data());
		::inet_ntop(AF_INET, addr, const_cast<char*>(sip.data()), sip.size());
	}
	bool storage_2_sockaddr(addr_storage_t* storage, void* p) {
		auto* addr = static_cast<struct sockaddr_in*>(p);
		addr->sin_family = AF_INET;
		addr->sin_port =  htons(storage->port);
		return ::inet_pton(AF_INET, storage->ip.data(), &(addr->sin_addr)) == 1;
	}
}