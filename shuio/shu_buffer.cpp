#include "shu_buffer.h"
#include "shu_common.h"
#include <cstring>
#include <vector>

namespace shu {
	std::size_t socket_buffer::size()
	{
		return data_.size();
	}

	char* socket_buffer::cast() {
		return data_.data();
	}

	std::span<char> socket_buffer::ready()
	{
		return std::span<char>(cast(), pos_);
	}

	std::span<char> socket_buffer::prepare(std::size_t sz) {
		if (data_.size() < (sz + pos_)) {
			data_.resize(pos_ + sz);
		}
		return prepare();
	}

	std::span<char> socket_buffer::prepare()
	{
		return std::span<char>(cast() + pos_, data_.size() - pos_);
	}

	void socket_buffer::compress(std::size_t sz)
	{
		data_.resize(sz);
		pos_ = std::min(sz, pos_);
	}

	std::size_t socket_buffer::commit(std::size_t sz) {
		if (sz == 0) return 0;
		sz = std::min<>(sz, data_.size() - pos_);
		pos_ += sz;
		return sz;
	}

	std::size_t socket_buffer::commit()
	{
		auto sz = data_.size() - pos_;
		pos_ = data_.size();
		return sz;
	}

	std::size_t socket_buffer::consume(std::size_t sz) {
		sz = std::min<>(sz, pos_);
		pos_ -= sz;
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

	socket_buffer::socket_buffer(std::size_t sz):data_(sz),pos_(0) {
		
	}

	socket_buffer::socket_buffer(socket_buffer&& other) noexcept
	{
		data_ = std::exchange(other.data_, {});
		pos_ = std::exchange(other.pos_, 0);
	}

	socket_buffer& socket_buffer::operator=(socket_buffer&& other) noexcept {
		std::swap(data_, other.data_);
		std::swap(pos_, other.pos_);
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