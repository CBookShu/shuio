#include "shu_buffer.h"

namespace shu {
	struct socket_buffer::buffer_t {
		std::size_t sz;
		std::size_t pos;
		//char data[0];
	};

	std::size_t socket_buffer::size()
	{
		return data->sz;
	}

	char* socket_buffer::cast() {
		return reinterpret_cast<char*>(data + 1);
	}

	std::span<char> socket_buffer::ready()
	{
		return std::span<char>(cast(), data->pos);
	}

	std::span<char> socket_buffer::prepare(std::size_t sz) {
		if (data->sz < (sz + data->pos)) {
			auto* p = ::realloc(data, sizeof(buffer_t) + sz + data->pos);
			assert(p);
			data = reinterpret_cast<buffer_t*>(p);
			data->sz = sz + data->pos;
		}
		return prepare();
	}

	std::span<char> socket_buffer::prepare()
	{
		return std::span<char>(cast() + data->pos, data->sz - data->pos);
	}

	void socket_buffer::compress(std::size_t sz)
	{
		auto* p = ::realloc(data, sizeof(buffer_t) + sz);
		assert(p);
		data = reinterpret_cast<buffer_t*>(p);
		data->sz = sz;
	}

	std::size_t socket_buffer::commit(std::size_t sz) {
		if (sz == 0) return 0;
		sz = std::min<>(sz, data->sz - data->pos);
		data->pos += sz;
		return sz;
	}

	std::size_t socket_buffer::consume(std::size_t sz) {
		sz = std::min<>(sz, data->pos);
		data->pos -= sz;
		return sz;
	}

	socket_buffer::~socket_buffer() {
		if (data) {
			::free(data);
		}
	}

	socket_buffer::socket_buffer(std::size_t sz) {
		data = (buffer_t*)::malloc(sizeof(buffer_t) + sz);
		if (data) [[likely]] {
			data->sz = sz;
			data->pos = 0;
		}
		else {
			// ÄÚ´æ²»×ã?
			::abort();
		}
	}

	socket_buffer::socket_buffer(socket_buffer&& other) noexcept
	{
		data = other.data;
		other.data = nullptr;
	}

	couple_buffer::couple_buffer(std::size_t init_size)
		:b{ {init_size},{init_size} }, curidx(-1)
	{

	}

	couple_buffer::couple_buffer(couple_buffer&& other) noexcept
		:b{ {std::move(other.b[0])}, {std::move(other.b[1])} }
	{
		curidx = other.curidx;
	}

	couple_buffer::~couple_buffer()
	{
	}

	socket_buffer* couple_buffer::curbuf()
	{
		if (curidx == -1) {
			curidx = 0;
		}
		return b + curidx;
	}

	socket_buffer* couple_buffer::otherbuf()
	{
		if (curidx == -1) {
			return nullptr;
		}
		return b + (1 - curidx);
	}

	void couple_buffer::next()
	{
		if (curidx == -1) {
			return;
		}
		curidx = 1 - curidx;
	}

	void couple_buffer::reset()
	{
		curidx = -1;
	}
};