#include "shuio/shu_common.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <filesystem>
#include <sstream>

namespace shu {
	int s_last_error()
	{
		auto e = errno;
		errno = 0;
		return e; 
	}

	void panic(bool con,std::string_view msg, std::source_location call) noexcept {
		if (!con) [[unlikely]] {
			auto path = std::filesystem::path(call.file_name());
			char buff[1024];
			if (msg.empty()) {
				snprintf(buff, sizeof(buff)-1, "Panic %s:%d %s", 
					path.filename().string().c_str(),
					call.line(),
					call.function_name());
			}
			else {
				snprintf(buff, sizeof(buff)-1, "Panic %s %s:%d %s", 
					msg.data(),
					path.filename().string().c_str(),
					call.line(),
					call.function_name());
			}
			std::cerr << buff;
			std::cerr << std::endl;

			// C++23 std::unreachable()
			std::terminate();
		}
	}

	void sockaddr_2_storage(void* p, addr_storage_t* storage) {
		auto* addr_common = static_cast<sockaddr_storage*>(p);
		if (addr_common->ss_family == AF_INET) {
			auto* addr = static_cast<struct sockaddr_in*>(p);
			storage->port = ntohs(addr->sin_port);
			std::string_view sip(storage->ip.data());
			::inet_ntop(addr_common->ss_family, addr, storage->ip.data(), storage->ip.size());
		} else if (addr_common->ss_family == AF_INET6) {
			auto* addr = static_cast<struct sockaddr_in6*>(p);
			storage->port = ntohs(addr->sin6_port);
			std::string_view sip(storage->ip.data());
			::inet_ntop(addr_common->ss_family, addr, storage->ip.data(), storage->ip.size());
		} else {
			shu::panic(false, std::string("error family:") + std::to_string(addr_common->ss_family));
		}
	}

	bool storage_2_sockaddr(addr_storage_t* storage, void* p) {
		int family = storage->family();
		if(family == AF_INET) {
			auto* addr = static_cast<struct sockaddr_in*>(p);
			addr->sin_family = family;
			addr->sin_port =  htons(storage->port);
			return ::inet_pton(family, storage->ip.data(), &(addr->sin_addr)) == 1;
		} else if (family == AF_INET6) {
			auto* addr = static_cast<struct sockaddr_in6*>(p);
			addr->sin6_family = family;
			addr->sin6_port =  htons(storage->port);
			return ::inet_pton(family, storage->ip.data(), &(addr->sin6_addr)) == 1;
		} else {
			shu::panic(false, std::string("error family:") + std::to_string(family));
			return false;
		}
	}

	int addr_storage_t::family()  {
		std::string_view s(ip.data());
		auto pos = s.find('.');
		if (pos != std::string_view::npos) {
			return AF_INET;
		}
		pos = s.find(':');
		if (pos != std::string_view::npos) {
			return AF_INET6;
		}
		return 0;
	}
}