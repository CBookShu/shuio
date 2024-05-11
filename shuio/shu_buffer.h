#pragma once
#include <iostream>
#include <span>
#include <cctype>
#include <vector>
#include <string>
#include <memory_resource>

namespace shu {
#ifdef _WIN32
	// cast to WSABUF
	typedef struct buffer_t {
		unsigned long size;
		const char* p;
	} buffer_t;
#else
	// cast to struct iovec 
	typedef struct buffer_t {
		const char* p;
  		size_t size;
	} buffer_t;
#endif
	using buffers_t = std::span<buffer_t>;

	template <typename T>
	concept c_buffers_can_copy = requires (T t, const char* b, const char* e, int sz) {
		t.assign(b, e);
		t.insert(t.end(), b, e);
		t.size();
		t.resize(sz);
	};

	// string, vector 等
	static_assert(c_buffers_can_copy<std::string>);
	static_assert(c_buffers_can_copy<std::vector<char>>);
	static_assert(c_buffers_can_copy<std::pmr::string>);
	static_assert(c_buffers_can_copy<std::pmr::vector<char>>);

	template <typename T>
	requires c_buffers_can_copy<T>
	inline std::size_t copy_from_buffers(T& s, buffers_t bufs) {
		if (bufs.empty()) return 0;
		s.assign(bufs[0].p, bufs[0].p + bufs[0].size);
		for (int i = 1; i < bufs.size(); ++i) {
			s.insert(s.end(), bufs[i].p, bufs[i].p + bufs[i].size);
		}
		return s.size();
	}

	// 从1 buffer开始分配，如果自定义了alloc 那么s 很可能已经保存了buf1的内容
	template <typename T>
	requires c_buffers_can_copy<T>
	inline std::size_t copy_from_buffers_1(T& s, buffers_t bufs) {
		if (bufs.empty()) return 0;
		s.resize(bufs[0].size);
		for (int i = 1; i < bufs.size(); ++i) {
			s.insert(s.end(), bufs[i].p, bufs[i].p + bufs[i].size);
		}
		return s.size();
	}
};

