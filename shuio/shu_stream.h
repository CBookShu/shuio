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
		using func_op_t = std::function<void(socket_io_result_t)>;
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
		void start(
			sloop*, ssocket*, sstream_opt opt, 
			stream_ctx_t&& stream_event);

		auto get_ud() -> std::any*;

		// TODO: 支持read和write 重复多次调用
		bool read(buffer_t buf, func_op_t&& cb);
		bool read(std::span<buffer_t> bufs, func_op_t&& cb);

		bool write(buffer_t buf, func_op_t&& cb);
		bool write(std::span<buffer_t> bufs, func_op_t&& cb);
		// call after start read
		void stop();
	};
};


