#include "shuio/shu_buffer.h"
#include "win32_detail.h"

namespace shu {
	static_assert(sizeof(buffer_t) == sizeof(WSABUF));
    static_assert(sizeof(buffer_t::p) == sizeof(WSABUF::buf));
    static_assert(sizeof(buffer_t::size) == sizeof(WSABUF::len));
};