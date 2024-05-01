#pragma once
#include "shu_common.h"
#include "shu_socket.h"

#include <memory>

namespace shu {

	class sloop;
	class ssocket;

	class sacceptor
	{
		struct sacceptor_t;
		sacceptor_t* s_;
		S_DISABLE_COPY(sacceptor);
	public:
		using func_newclient_t = \
			std::function<void(socket_io_result_t,ssocket*, const addr_pair_t&)>;
		using func_close_t = std::function<void(sacceptor*)>;

		struct event_ctx {
			func_close_t evClose;
			func_newclient_t evConn;
		};

		sacceptor();
		sacceptor(sacceptor&&) noexcept;
		~sacceptor();

		int start(sloop*,event_ctx&&,addr_storage_t);
		auto loop() -> sloop*;
		auto addr() -> const addr_storage_t&;
		void stop();
	};
};


