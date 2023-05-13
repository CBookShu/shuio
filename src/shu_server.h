#pragma once
#include "shu_common.h"

namespace shu {

	struct sserver_opt {
		addr_storage_t addr;
	};
	class sloop;
	struct sserver_runnable;
	class ssocket;

	class sserver
	{
		struct sserver_t;
		sserver_t* _s;
		S_DISABLE_COPY(sserver);
	public:
		sserver(sserver_opt);
		sserver(sserver&&) noexcept;
		~sserver();

		auto option() -> const sserver_opt*;
		auto loop() -> sloop*;
		auto sock() -> ssocket*;
		auto start(sloop*, sserver_runnable*,addr_storage_t) -> bool;
	};

	struct sserver_runnable {
		virtual ~sserver_runnable() {}
		virtual void new_client(socket_io_result_t, sserver*, ssocket*, addr_pair_t) noexcept = 0;
		virtual void destroy() {
			delete this;
		}
	};
};


