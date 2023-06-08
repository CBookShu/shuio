#pragma once
#include "shu_common.h"
#include <memory>
#include <span>

namespace shu {

	struct sstream_opt {
		addr_pair_t addr;
		// 读写缓存buffer上限
		std::size_t read_buffer_limit = 1024 * 1024 * 20;
		std::size_t write_buffer_limit = 1024 * 1024 * 20;
		// 读写缓存初始大小
		std::size_t read_buffer_init = 4096;
		std::size_t write_buffer_init = 4096;
		// 每次读的大小
		std::size_t read_buffer_count_per_op = 4096;
	};

	class sloop;
	class ssocket;
	class socket_buffer;
	struct sstream_runable;
	class sstream : public std::enable_shared_from_this<sstream>
	{
		struct sstream_t;
		sstream_t* _s;
		S_DISABLE_COPY(sstream);
	public:
		using SPtr = std::shared_ptr<sstream>;
		sstream(sloop*,ssocket*,sstream_opt opt);
		sstream(sstream&&) noexcept;
		~sstream();

		auto handle() -> struct sstream_t*;
		auto option() -> const sstream_opt*;
		auto option() const -> const sstream_opt*;
		auto loop() -> sloop*;
		auto sock() -> ssocket*;

		auto readbuffer() -> socket_buffer*;
		auto writebuffer() -> std::span< socket_buffer*>;

		// just call once after new
		auto start_read(sstream_runable*) -> void;
		// call after start read
		auto write(socket_buffer*) -> bool;
		// call after start read
		auto close() -> void;
	};

	struct sstream_runable {
		virtual ~sstream_runable() {}
		virtual void on_read(socket_io_result_t, sstream::SPtr) noexcept = 0;
		virtual void on_write(socket_io_result_t, sstream::SPtr) noexcept = 0;
		virtual void on_close(const sstream*) noexcept = 0;
		virtual void destroy() noexcept {
			delete this;
		}
	};
};


