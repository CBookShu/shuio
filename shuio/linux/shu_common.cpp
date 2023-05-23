#include "shuio/shu_common.h"
#include <cerrno>

namespace shu {
	int s_last_error()
	{
		return errno;
	}
}