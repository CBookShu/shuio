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
		std::weak_ptr<sserver_t> s_;
		S_DISABLE_COPY(sserver);
	public:
		using func_newclient_t = \
			std::function<void(socket_io_result_t,std::unique_ptr<ssocket>, addr_pair_t)>;

		sserver();
		sserver(sserver&&) noexcept;
		~sserver();

		void start(sloop*,func_newclient_t&& ,addr_storage_t);
		void stop();
	};
};


