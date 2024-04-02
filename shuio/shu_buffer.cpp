#include "shu_buffer.h"
#include "shu_common.h"
#include <cstring>

namespace shu {
	extern "C" {
		struct socket_buffer::buffer_t {
			std::size_t sz;
			std::size_t pos;
			char data[0];
		};
	}

	std::size_t socket_buffer::size()
	{
		return data_->sz;
	}

	char* socket_buffer::cast() {
		return data_->data;
	}

	std::span<char> socket_buffer::ready()
	{
		return std::span<char>(cast(), data_->pos);
	}

	std::span<char> socket_buffer::prepare(std::size_t sz) {
		if (data_->sz < (sz + data_->pos)) {
			auto* p = ::realloc(data_, sizeof(buffer_t) + sz + data_->pos);
			assert(p);
			shu::exception_check(p, "oom");
			data_ = reinterpret_cast<buffer_t*>(p);
			data_->sz = sz + data_->pos;
		}
		return prepare();
	}

	std::span<char> socket_buffer::prepare()
	{
		return std::span<char>(cast() + data_->pos, data_->sz - data_->pos);
	}

	void socket_buffer::compress(std::size_t sz)
	{
		auto* p = ::realloc(data_, sizeof(buffer_t) + sz);
		assert(p);
		shu::exception_check(p, "oom");
		data_ = reinterpret_cast<buffer_t*>(p);
		data_->sz = sz;
	}

	std::size_t socket_buffer::commit(std::size_t sz) {
		if (sz == 0) return 0;
		sz = std::min<>(sz, data_->sz - data_->pos);
		data_->pos += sz;
		return sz;
	}

	std::size_t socket_buffer::commit()
	{
		auto sz = data_->sz - data_->pos;
		data_->pos = data_->sz;
		return sz;
	}

	std::size_t socket_buffer::consume(std::size_t sz) {
		sz = std::min<>(sz, data_->pos);
		data_->pos -= sz;
		return sz;
	}

	socket_buffer& socket_buffer::prepare(std::string_view s) {
		auto pre = prepare(s.size());
		std::copy(s.begin(), s.end(), pre.begin());
		return *this;
	}

	socket_buffer& socket_buffer::prepare(std::span<char> s) {
		auto pre = prepare(s.size());
		std::copy(s.begin(), s.end(), pre.begin());
		return *this;
	}

	socket_buffer::~socket_buffer() {
		if (data_) {
			::free(data_);
		}
	}

	socket_buffer socket_buffer::copy()
	{
		socket_buffer c(data_->sz);
		std::memcpy(c.data_, data_, data_->sz + sizeof(buffer_t));
		return c;
	}

	socket_buffer::socket_buffer(const char* s):socket_buffer(strlen(s)){
		prepare(s);
	}

	socket_buffer::socket_buffer(std::string_view s) :socket_buffer(s.size()) {
		prepare(s);
	}

	socket_buffer::socket_buffer(const std::string& s) :socket_buffer(s.size()) {
		prepare(s);
	}

	socket_buffer::socket_buffer(std::size_t sz) {
		data_ = (buffer_t*)::malloc(sizeof(buffer_t) + sz);
		if (data_) [[likely]] {
			data_->sz = sz;
			data_->pos = 0;
		}
		else {
			// 内存不足?
			::abort();
		}
	}

	socket_buffer::socket_buffer(socket_buffer&& other) noexcept
	{
		data_ = std::exchange(other.data_, nullptr);
	}

	socket_buffer& socket_buffer::operator=(socket_buffer&& other) noexcept {
		if (data_) {
			::free(data_);
		}
		data_ = std::exchange(other.data_, nullptr);
		return *this;
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