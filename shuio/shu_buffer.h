#pragma once
#include <iostream>
#include <cassert>
#include <span>
#include <vector>

namespace shu {

	class socket_buffer {
		std::vector<char> data_;
		std::size_t pos_;
	public:
		std::size_t size();
		char* cast();

		// 0~pos
		std::span<char> ready();

		// pos~end
		std::span<char> prepare(std::size_t sz);
		std::span<char> prepare();
		socket_buffer& prepare(std::string_view s);
		socket_buffer& prepare(std::span<char> s);

		// resize
		void compress(std::size_t sz);
		// pos+=sz
		std::size_t commit(std::size_t sz);
		std::size_t commit();
		// pos-=sz
		std::size_t consume(std::size_t sz);

		socket_buffer(const char* s);
		socket_buffer(std::string_view s);
		socket_buffer(const std::string& s);
		socket_buffer(std::size_t sz);
		socket_buffer(socket_buffer&& other) noexcept;
		socket_buffer& operator = (socket_buffer&&) noexcept;
		~socket_buffer();

		socket_buffer(socket_buffer& other) = delete;
		socket_buffer& operator = (socket_buffer&) = delete;
	};

	class couple_buffer {
		socket_buffer b[2];
		int curidx;
	public:
		couple_buffer(std::size_t init_size);
		couple_buffer(couple_buffer&&) noexcept;
		~couple_buffer();

		couple_buffer(couple_buffer& other) = delete;
		couple_buffer* operator = (couple_buffer&) = delete;

		socket_buffer* curbuf();
		socket_buffer* otherbuf();
		void next();
		void reset();
	};

	typedef struct buffer_t {
		char* 	p;
		int 	size;
	}buffer_t;

};

