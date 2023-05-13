#pragma once
#include <iostream>
#include <cassert>
#include <span>

namespace shu {

	class socket_buffer {
		struct buffer_t;
		buffer_t* data;
	public:
		std::size_t size();
		char* cast();

		std::span<char> ready();
		std::span<char> prepare(std::size_t sz);
		std::span<char> prepare();
		void compress(std::size_t sz);
		std::size_t commit(std::size_t sz);
		std::size_t consume(std::size_t sz);

		socket_buffer(std::size_t sz);
		socket_buffer(socket_buffer&& other) noexcept;
		~socket_buffer();

		socket_buffer(socket_buffer& other) = delete;
		socket_buffer& operator = (socket_buffer&) = delete;
		socket_buffer& operator = (socket_buffer&&) = delete;
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

};

