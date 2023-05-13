#include "shuio/shu_common.h"
#include <WinSock2.h>

namespace shu {
	int s_last_error()
	{
		return ::WSAGetLastError();
	}
}