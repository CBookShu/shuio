#include "shuio/shu_common.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <filesystem>
#include <sstream>

namespace shu {
	int s_last_error()
	{
		return errno;
	}

	void exception_check(bool con,std::string_view msg, std::source_location call) noexcept {
		if (!con) [[unlikely]] {
			auto path = std::filesystem::path(call.file_name());
			std::stringstream ss;
			if (msg.empty()) {
				std::cerr
				<< "Exception @file:" << path.filename().string()
				<< " @line:" << call.line()
				<< " @func:" << call.function_name();
			}
			else {
				std::cerr
				<< "Exception @msg:" << msg
				<< " @file:" << path.filename().string()
				<< " @line:" << call.line()
				<< " @func:" << call.function_name();
			}
			std::cerr << std::endl;

			// C++23 std::unreachable()
			std::terminate();
		}
	}

	void sockaddr_2_storage(void* p, addr_storage_t* storage) {
		auto* addr = static_cast<struct sockaddr_in*>(p);
		storage->port = ntohl(addr->sin_port);
		::inet_ntop(AF_INET, addr, storage->ip.data(), storage->ip.size() );
	}
	bool storage_2_sockaddr(addr_storage_t* storage, void* p) {
		auto* addr = static_cast<struct sockaddr_in*>(p);
		addr->sin_family = AF_INET;
		addr->sin_port =  htons(storage->port);
		storage->ip.resize(64);
		return ::inet_pton(AF_INET, storage->ip.data(), &(addr->sin_addr)) == 1;
	}
}