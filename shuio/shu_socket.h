#pragma once
#include <iostream>
#include <cstdint>
#include "shu_common.h"

namespace shu {

	class sloop;;
	typedef union ssocket_opt {
		std::uint32_t data;
		struct {
			unsigned noblock : 1;
			unsigned reuse_addr : 1;
			unsigned reuse_port : 1;
			unsigned nodelay : 1;
			unsigned v6 : 1;
		}flags;
	}ssocket_opt;

	enum class shutdown_type {
		shutdown_read,
		shutdown_write,
		shutdown_both,
	};

	class ssocket
	{
		struct ssocket_t;
		ssocket_t* ss_;
		S_DISABLE_COPY(ssocket);
	public:
		ssocket();
		ssocket(ssocket&& other)  noexcept;
		~ssocket();

		auto handle() -> ssocket_t*;

		// iptype: 0 tcp, 1 udp
		void init(bool udp = false, bool v6 = false);
		auto option() -> const ssocket_opt*;
		int noblock(bool);
		int reuse_addr(bool);
		int reuse_port(bool);	// win32 应该是不支持的
		int keepalive(bool enable,int delay);
		int nodelay(bool);
		int bind(void*, std::size_t);
		int listen();
		void close();
		bool valid();
		int shutdown(shutdown_type how = shutdown_type::shutdown_both);
	};
};

