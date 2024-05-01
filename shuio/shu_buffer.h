#pragma once
#include <iostream>
#include <span>
#include <cctype>

namespace shu {
#ifdef _WIN32
	// cast to WSABUF
	typedef struct buffer_t {
		unsigned long size;
		char* p;
	} buffer_t;
#else
	// cast to struct iovec 
	typedef struct buffer_t {
		char* p;
  		size_t size;
	} buffer_t;
#endif
	using buffers_t = std::span<buffer_t>;
};

