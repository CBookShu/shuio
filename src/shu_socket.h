#pragma once
#include <iostream>
#include <cstdint>
#include "shu_common.h"

namespace shu {

	typedef union ssocket_opt {
		std::uint32_t data;
		struct {
			unsigned udp : 1;
			unsigned noblock : 1;
			unsigned reuse_addr : 1;
			unsigned reuse_port : 1;
			unsigned nodelay : 1;
		}flags;
	}ssocket_opt;

	class ssocket
	{
		struct ssocket_t;
		ssocket_t* _ss;
		S_DISABLE_COPY(ssocket);
	public:
		ssocket(ssocket_opt opt);
		ssocket(ssocket&& other)  noexcept;
		~ssocket();

		auto handle() -> ssocket_t*;

		// iptype: 0 tcp, 1 udp
		auto init(int iptype) -> bool;
		auto option() -> const ssocket_opt*;
		auto noblock(bool) -> bool;
		auto reuse_addr(bool) -> bool;
		auto reuse_port(bool) -> bool;	// win32 应该是不支持的
		auto nodelay(bool) -> bool;
	};
};

