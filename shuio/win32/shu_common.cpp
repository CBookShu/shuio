#include "shuio/shu_common.h"
#include <WinSock2.h>
#include <ws2tcpip.h>

namespace shu {
	int s_last_error()
	{
		return ::WSAGetLastError();
	}

	void sockaddr_2_storage(void* p, addr_storage_t* storage) {
		auto* addr = static_cast<struct sockaddr_in*>(p);
		storage->port = ntohl(addr->sin_port);
		::inet_ntop(AF_INET, addr, storage->ip, std::size(storage->ip) -1 );
	}
	bool storage_2_sockaddr(addr_storage_t* storage, void* p) {
		auto* addr = static_cast<struct sockaddr_in*>(p);
		addr->sin_family = AF_INET;
		addr->sin_port =  htons(storage->port);
		return ::inet_pton(AF_INET, storage->ip, &(addr->sin_addr)) == 1;
	}
}