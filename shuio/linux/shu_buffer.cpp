#include "shuio/shu_buffer.h"
#include "linux_detail.h"

namespace shu {
	static_assert(sizeof(buffer_t) == sizeof(iovec));
    static_assert(sizeof(buffer_t::p) == sizeof(iovec::iov_base));
    static_assert(sizeof(buffer_t::size) == sizeof(iovec::iov_len));
};