#pragma once
#include "shu_common.h"
#include "shu_buffer.h"
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
	struct read_ctx_t {
		socket_buffer& buf;
	};
	struct write_ctx_t {
		std::vector<socket_buffer>& bufs;
	};
	class sstream
	{
		struct sstream_t;
		std::weak_ptr<sstream_t> s_;
		S_DISABLE_COPY(sstream);
	public:

		using func_read_t = std::function<void(socket_io_result_t, read_ctx_t&)>;
		using func_write_t = std::function<void(socket_io_result_t, write_ctx_t&)>;

		sstream();
		sstream(sstream&&) noexcept;
		~sstream();

		// just call once after new
		void start(
			sloop*, ssocket*, sstream_opt opt, 
			func_read_t&& rcb, func_write_t&& wcb);

		// call after start read
		void write(socket_buffer&& );
		// call after start read
		void stop();
	};
};


