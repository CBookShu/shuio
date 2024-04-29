#pragma once
#include "shu_common.h"
#include "shu_socket.h"

#include <memory>

namespace shu {

	class sloop;
	class ssocket;

	class sserver
	{
		struct sserver_t;
		sserver_t* s_;
		S_DISABLE_COPY(sserver);
	public:
		using func_newclient_t = \
			std::function<void(socket_io_result_t,ssocket*, addr_pair_t)>;
		using func_close_t = std::function<void(sserver*)>;

		struct event_ctx {
			func_close_t evClose;
			func_newclient_t evConn;
		};

		sserver();
		sserver(sserver&&) noexcept;
		~sserver();

		bool start(sloop*,event_ctx&&,addr_storage_t);
		auto loop() -> sloop*;
		void stop();
	};
};


