#include "shuio/shu_loop.h"
#include "win32_detail.h"

#include <thread>
#include <cassert>
#include <map>
#include <unordered_map>

namespace shu {

	struct local_timer {
		micsec_t max_expired_time;
		std::atomic_uint64_t timerseq{ 0 };
		std::map<tp_t, std::unordered_map<std::uint64_t, sloop_runable*>> timers;
		std::jthread timer_thread;
		HANDLE win32_timer_handle = INVALID_HANDLE_VALUE;
		std::atomic_bool stop = false;

		void _set_timer(auto now, bool first = false) {
			using namespace std::chrono;
			auto expire = max_expired_time + now;
			expire = timer_min_expire(expire);
			auto diff = duration_cast<micsec_t>(expire - now);
			if (first || diff < max_expired_time) {
				// QuadPart的单位是100纳秒
				LARGE_INTEGER timeout{};
				timeout.QuadPart = -duration_cast<nasec_t>(diff).count() / 100;
				auto ms = duration_cast<milsec_t>(max_expired_time);
				LONG p = static_cast<LONG>(ms.count());
				::SetWaitableTimer(win32_timer_handle,
					&timeout, p, 0, 0, FALSE);
			}
		}

		void start_timer(auto&& f, micsec_t init_wait) {
			if (win32_timer_handle != INVALID_HANDLE_VALUE) {
				return;
			}
			win32_timer_handle = ::CreateWaitableTimer(0, FALSE, 0);
			max_expired_time = init_wait;
			_set_timer(systime_t::now(), true);
			timer_thread = std::jthread([this, f = std::move(f)]() {
				while (!stop) {
					if (::WaitForSingleObject(win32_timer_handle, INFINITE) == WAIT_OBJECT_0) {
						f();
					}
				}
			});
		}
		void timer_call() {
			auto now = systime_t::now();
			auto e = timers.upper_bound(now);
			std::vector< sloop_runable*> runs;
			for (auto b = timers.begin(); b != e; ++b) {
				for (auto& it : b->second) {
					runs.push_back(it.second);
				}
			}
			// !! 这里哪怕runs是空的，也要重新再定时一次！
			// 因为timer的定时器和now的判断未必那么准确！
			timers.erase(timers.begin(), e);
			for (auto& r : runs) {
				r->run();
				r->destroy();
			}
			// 重新定时
			_set_timer(now);
		}
		void stop_timer() {
			stop = true;
			LARGE_INTEGER timeout;
			timeout.QuadPart = 1;
			::SetWaitableTimer(win32_timer_handle, &timeout, 1, 0, 0, FALSE);
			timer_thread.join();
			::CloseHandle(win32_timer_handle);
			for (auto& it1 : timers) {
				for (auto& it2 : it1.second) {
					it2.second->destroy();
				}
			}
			timers.clear();
		}
		auto timer_get_seq() -> std::uint64_t {
			for (;;) {
				auto r = timerseq.fetch_add(1);
				if (r == 0) {
					continue;
				}
				return r;
			}
		}
		auto timer_min_expire(tp_t expire) -> tp_t {
			auto it = timers.begin();
			if (it == timers.end()) {
				return expire;
			}
			return std::min<>(it->first, expire);
		}
		auto timer_add(tp_t now, sloop_timer_t_id id, sloop_runable* r) -> void {
			using namespace std::chrono;
			auto expire = timer_min_expire(id.expire);
			timers[id.expire][id.id] = r;
			_set_timer(now);
		}
		auto timer_erase(sloop_timer_t_id id) -> void {
			auto it = timers.find(id.expire);
			if (it == timers.end()) {
				return;
			}
			auto it1 = it->second.find(id.id);
			if (it1 == it->second.end()) {
				return;
			}
			it1->second->destroy();
			it->second.erase(it1);
			if (it->second.empty()) {
				timers.erase(it);
			}
			_set_timer(systime_t::now());
		}
	};

	struct task_user_op : public OVERLAPPED {
		sloop_runable* cb;
	};

	struct sloop::sloop_t : public iocp_navite_t {
		std::thread::id loop_tid;
		struct sloop_opt opt;
		local_timer timer;
	};

	sloop::sloop(sloop_opt opt)
	{
		_loop = new sloop_t{};
		_loop->iocp = INVALID_HANDLE_VALUE;
		_loop->iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		assert(_loop->iocp != INVALID_HANDLE_VALUE);
	}

	sloop::sloop(sloop&& other)  noexcept
	{
		_loop = std::exchange(other._loop, nullptr);
	}

	sloop::~sloop()
	{
		if(!_loop)	return;
		if (_loop->iocp != INVALID_HANDLE_VALUE) {
			::CloseHandle(_loop->iocp);
		}
		delete _loop;
	}

	auto sloop::handle() -> sloop_t*
	{
		return _loop;
	}

	auto sloop::post(sloop_runable*r) -> bool
	{
		auto op_ptr = std::make_unique<task_user_op>();
		op_ptr->cb = r;
		auto res = ::PostQueuedCompletionStatus(_loop->iocp, 0, IOCP_OP_TYPE::TASK_TYPE, op_ptr.get());
		if (!res) {
			r->destroy();
			op_ptr.reset();
		}
		else {
			op_ptr.release();
		}
		return res;
	}

	auto sloop::dispatch(sloop_runable* r) -> bool
	{
		if (_loop->loop_tid == std::this_thread::get_id()) {
			r->run();
			r->destroy();
			return true;
		}
		return post(r);
	}

	auto sloop::add_timer(sloop_runable* r, std::chrono::milliseconds time) -> sloop_timer_t_id
	{
		auto now = systime_t::now();

		sloop_timer_t_id id;
		id.id = _loop->timer.timer_get_seq();
		id.expire = now + time;

		auto deletor_f = [](auto* t) {
			t->destroy(); 
		};
		std::unique_ptr<sloop_runable, decltype(deletor_f)> uptr_r(r, deletor_f);
		auto ret = dispatch_f([now, id, time, this, uptr_r = std::move(uptr_r)]()mutable{
			_loop->timer.timer_add(now, id, uptr_r.release());
		});
		if (!ret) {
			return {};
		}
		return id;
	}

	auto sloop::cancel_timer(sloop_timer_t_id id) -> void
	{
		dispatch_f([id, this]() {
			_loop->timer.timer_erase(id);
		});
	}

	auto sloop::run() -> void
	{
		_loop->loop_tid = std::this_thread::get_id();

		_loop->timer.max_expired_time = _loop->opt.max_expired_time;
		_loop->timer.start_timer([this]() {
			// 定时器到了需要触发iocp循环
			::PostQueuedCompletionStatus(_loop->iocp, 0, IOCP_OP_TYPE::TIMER_TYPE, nullptr);
		}, _loop->opt.max_expired_time);
		
		// iocp loop begin
		for (;;) {
			OVERLAPPED_ENTRY overlappeds[128];
			ULONG count;
			auto r = ::GetQueuedCompletionStatusEx(_loop->iocp, overlappeds, _countof(overlappeds), &count, INFINITE, false);
			if (!r) {
				// 会有什么错误么?
				auto e = WSAGetLastError();
				continue;
			}
			bool hasstop = false;
			bool hastimer = false;
			// first do socket op
			int pure_idx[128] = { 0 };
			std::size_t pos = 0;

			for (std::size_t i = 0; i < count; ++i) {
				auto type = IOCP_OP_TYPE(overlappeds[i].lpCompletionKey);
				if (type == IOCP_OP_TYPE::TIMER_TYPE) {
					hastimer = true;
				}
				else if (type == IOCP_OP_TYPE::STOP_TYPE) {
					hasstop = true;
				}
				else if (type == IOCP_OP_TYPE::SOCKET_TYPE) {
					assert(overlappeds[i].lpOverlapped);
					// socket type 一定要有overlapped
					auto* ud = reinterpret_cast<socket_user_op*>(overlappeds[i].lpOverlapped);
					if (ud) {
						// IO 的lpOverlapped 由io自己负责释放
						ud->cb->run(overlappeds + i);
					}
				}
				else if (type == IOCP_OP_TYPE::TASK_TYPE) {
					pure_idx[pos++] = i;
				}
				else {
					// error
					assert(false);
				}
			}
			for (std::size_t i = 0; i < pos; ++i) {
				auto* ud = reinterpret_cast<task_user_op*>(overlappeds[pure_idx[i]].lpOverlapped);
				// 纯粹的op操作
				if (ud) {
					ud->cb->run();
					ud->cb->destroy();
					delete ud;
				}
			}
			if (hastimer) {
				_loop->timer.timer_call();
			}
			if (hasstop) {
				break;
			}
		}

		// 清理所有的op
		for (;;) {
			OVERLAPPED_ENTRY overlappeds[128];
			ULONG count;
			auto r = ::GetQueuedCompletionStatusEx(_loop->iocp, overlappeds, _countof(overlappeds), &count, 0, false);
			if (!r) {
				auto e = WSAGetLastError();
				if (e == WSA_WAIT_TIMEOUT) {
					// all ok
					break;
				}
				// 如果真的出错了，这里有可能会泄漏op的内存！
				// TODO: 是否需要再次进行get 直到timeout?
				break;
			}
			for (ULONG i = 0; i < count; ++i) {
				if (!overlappeds[i].lpOverlapped) {
					auto* op = reinterpret_cast<sloop_runable*>(overlappeds[i].lpCompletionKey);
					// 不再执行，直接抛弃掉！
					op->destroy();
				}
				else {
					// socket op 不再操作！
				}
			}
		}

		// iocp loop end
		
		_loop->timer.stop_timer();
		_loop->loop_tid = {};
	}

	auto sloop::stop() -> void
	{
		::PostQueuedCompletionStatus(_loop->iocp, 0, IOCP_OP_TYPE::STOP_TYPE, nullptr);
	}

};