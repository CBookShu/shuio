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

	struct sloop_opt {
		// 超过该配置分钟的定时器，按照这个值进行设置
		micsec_t max_expired_time = sec_t(5);	// 默认5分钟
	};
	struct sloop_runable;
	struct sloop_timer_t_id{
		std::uint64_t id{ 0 };
		tp_t expire;
	};

	class sloop
	{
		struct sloop_t;
		sloop_t* _loop;
		S_DISABLE_COPY(sloop);
	public:
		sloop(sloop_opt opt);
		sloop(sloop&& other) noexcept;
		~sloop();

		auto handle() -> sloop_t*;

		auto post(sloop_runable*) -> bool;
		auto post_f(auto&& f) -> bool;

		auto dispatch(sloop_runable*) -> bool;
		auto dispatch_f(auto&& f) -> bool;

		auto add_timer(sloop_runable*, std::chrono::milliseconds) -> sloop_timer_t_id;
		auto add_timer_f(auto&& f, std::chrono::milliseconds) -> sloop_timer_t_id;
		auto cancel_timer(sloop_timer_t_id)->void;

		auto run() -> void;
		auto stop() -> void;
	};


	/// utils
	struct sloop_runable {
		virtual ~sloop_runable() {}
		virtual void run() noexcept = 0;
		virtual void destroy() noexcept {
			delete this;
		};
	};
	struct sloop_empty_runable : sloop_runable {
		virtual void run() noexcept {}
		virtual void destroy() noexcept {}
	};
	template <typename F>
	struct ioloop_functor : public sloop_runable {
		F f;
		ioloop_functor(F&& f) :f(std::move(f)) {}
		virtual void run() noexcept override {
			f();
		}
	};

	auto sloop::post_f(auto&& f) -> bool
	{
		auto pf = new ioloop_functor(std::move(f));
		return post(pf);
	}

	auto sloop::dispatch_f(auto&& f) -> bool
	{
		auto pf = new ioloop_functor(std::move(f));
		return dispatch(pf);
	}

	auto sloop::add_timer_f(auto&& f, std::chrono::milliseconds ms) -> sloop_timer_t_id
	{
		auto* pf = new ioloop_functor(std::move(f));
		return add_timer(pf, ms);
	}
};

