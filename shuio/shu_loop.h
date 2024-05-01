#pragma once
#include <iostream>
#include <chrono>
#include "shu_common.h"

namespace shu {
	
	using systime_t = std::chrono::system_clock;
	using tp_t = systime_t::time_point;
	using milsec_t = std::chrono::milliseconds;
	using sec_t = std::chrono::seconds;
	using minute_t = std::chrono::minutes;
	using micsec_t = std::chrono::microseconds;
	using nasec_t = std::chrono::nanoseconds;
	constexpr auto ms_1 = std::chrono::milliseconds(1);

	struct sloop_timer_t_id{
		std::chrono::steady_clock::time_point expire;
		std::uint64_t id{ 0 };
	};

	class sloop
	{
		struct sloop_t;
		sloop_t* loop_;
		S_DISABLE_COPY(sloop);
	public:
		sloop();
		sloop(sloop&& other) noexcept;
		~sloop();

		auto get_ud() -> std::any*;

		auto handle() -> sloop_t*;

		void post(func_t&& f);

		void dispatch(func_t&& f);

		auto add_timer(func_t&&, std::chrono::milliseconds) -> sloop_timer_t_id;
		auto cancel_timer(sloop_timer_t_id)->void;

		auto run() -> void;
		auto stop() -> void;

		void assert_thread(std::source_location call = std::source_location::current());
	};
};

