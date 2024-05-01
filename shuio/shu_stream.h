#pragma once
#include "shu_common.h"
#include "shu_buffer.h"
#include <memory>
#include <span>
#include <any>

namespace shu {

	struct sstream_opt {
		addr_pair_t addr;
	};

	class sloop;
	class ssocket;
	class sstream
	{
		struct sstream_t;
		sstream_t* s_;
		S_DISABLE_COPY(sstream);
	public:
		using func_on_read_t = std::function<void(socket_io_result_t, buffers_t)>;
		using func_on_write_t = std::function<void(socket_io_result_t)>;
		using func_alloc_t = std::function<void(int, buffer_t&)>;
		using func_close_t = std::function<void(sstream*)>;

		struct stream_ctx_t {
			func_close_t evClose;
		};

		sstream();
		sstream(sstream&&) noexcept;
		~sstream();

		auto option() -> sstream_opt;
		auto loop() -> sloop*;

		// just call once after new
		int start(
			sloop*, ssocket*, sstream_opt opt, 
			stream_ctx_t&& stream_event);

		auto get_ud() -> std::any*;
		
		int read(func_on_read_t&& cb, func_alloc_t&& alloc);

		bool write(buffer_t buf, func_on_write_t&& cb);
		bool write(buffers_t bufs, func_on_write_t&& cb);
		// call after start read
		void stop();
	};
};


