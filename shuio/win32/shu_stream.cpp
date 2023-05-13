#include "shuio/shu_stream.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_buffer.h"
#include <vector>
#include <atomic>

namespace shu {
	struct sstream_rw_op;
	struct sstream::sstream_t {
		sloop* loop;
		ssocket* sock;
		sstream_rw_op* op;
		sstream_opt opt;
	};

	struct sstream_rw_complete_t : public OVERLAPPED {
		std::shared_ptr<sstream> hold;
	};

	struct sstream_rw_op : iocp_sock_callback {
		sstream_runable* cb = nullptr;
		std::weak_ptr<sstream> ss;
		sstream_rw_complete_t* rd_complete = nullptr;
		sstream_rw_complete_t* wt_complete = nullptr;

		std::atomic_bool stop{ false };
		bool writing = false;
		bool reading = false;
		socket_buffer* rd_buf = nullptr;
		std::vector<socket_buffer*> wt_bufs;

		~sstream_rw_op() {
			if (cb) {
				cb->destroy();
			}
			if (rd_complete) {
				delete rd_complete;
			}
			if (wt_complete) {
				delete wt_complete;
			}
			if (rd_buf) {
				delete rd_buf;
			}
			for (auto& it : wt_bufs) {
				delete it;
			}
		}

		void init() {
			auto _s = ss.lock();
			rd_complete = new sstream_rw_complete_t{};
			wt_complete = new sstream_rw_complete_t{};
			rd_buf = new socket_buffer{ _s->option()->read_buffer_init};
			post_read();
		}
		void post_read() {
			assert(!reading);
			if (stop.load()) {
				// 将不再续上stream的声明周期
				return;
			}

			auto _s = ss.lock();
			assert(_s);

			if (_s->option()->addr.local.iptype == 1) {
				// udp
			}
			else {
				auto sock = wsa_cast_ssocket(_s->sock());
				auto s = rd_buf->prepare(_s->option()->read_buffer_count_per_op);
				WSABUF buf;
				buf.buf = s.data();
				buf.len = s.size_bytes();
				DWORD dwFlags = 0;
				DWORD dwBytes = 0;
				rd_complete->hold = _s;
				auto r = ::WSARecv(sock->s, &buf, 1, &dwBytes, &dwBytes, rd_complete, nullptr);
				auto e = WSAGetLastError();
				assert(r != SOCKET_ERROR || WSA_IO_PENDING == e);
				if (r == SOCKET_ERROR && WSA_IO_PENDING != e) [[unlikely]] {
					// 需要进行错误投递！
					rd_complete->hold.reset();
					socket_io_result_t res{ .bytes = 0,.err = -1, .naviteerr = e };
					cb->on_read(res, _s.get());
				}
				else {
					reading = true;
				}
			}
		}
		void post_write(socket_buffer* buf) {
			if (buf) {
				wt_bufs.push_back(buf);
			}
			if (stop.load()) [[unlikely]] {
				return;
			}
			if (writing) {
				return;
			}
			auto _s = ss.lock();
			assert(_s);

			if (_s->option()->addr.local.iptype == 1) {
				// udp
			}
			else {
				auto sock = wsa_cast_ssocket(_s->sock());
				std::vector<WSABUF> buffs(wt_bufs.size());
				std::size_t pos = 0;
				for (auto& it : wt_bufs) {
					auto& buf = buffs[pos++];
					auto data = it->ready();
					buf.len = data.size();
					buf.buf = data.data();
				}
				DWORD dwFlags = 0;
				DWORD dwBytes = 0;
				wt_complete->hold = _s;
				auto r = ::WSASend(sock->s, buffs.data(), buffs.size(), &dwBytes, dwFlags, wt_complete, nullptr);
				auto e = WSAGetLastError();
				assert(r != SOCKET_ERROR || WSA_IO_PENDING == e);
				if (r == SOCKET_ERROR && WSA_IO_PENDING != e) [[unlikely]] {
					// 需要进行错误投递！
					wt_complete->hold.reset();
					socket_io_result_t res{ .bytes = 0,.err = -1, .naviteerr = e };
					cb->on_write(res, _s.get());
				}
				else {
					writing = true;
				}
			}
		}
		virtual void run(OVERLAPPED_ENTRY* entry) noexcept override {
			auto _s = ss.lock();
			assert(_s);
			if (entry->lpOverlapped == rd_complete) {
				// read
				rd_complete->hold.reset();
				reading = false;
				rd_buf->commit(entry->dwNumberOfBytesTransferred);
				socket_io_result_t res{ .bytes = entry->dwNumberOfBytesTransferred };
				cb->on_read(res, _s.get());
				post_read();
			}
			else if (entry->lpOverlapped == wt_complete) {
				// write
				wt_complete->hold.reset();
				writing = false;
				socket_io_result_t res{ .bytes = entry->dwNumberOfBytesTransferred };
				cb->on_write(res, _s.get());
				auto total = entry->dwNumberOfBytesTransferred;
				auto eraseit = wt_bufs.begin();
				auto do_erase = false;
				for (auto it = wt_bufs.begin(); it != wt_bufs.end(); ++it) {
					auto* buf = (*it);
					total -= buf->consume(total);
					auto ready = buf->ready();
					if (ready.size() > 0) {
						// 没有写玩? 
						// 一般来说write 在socket 窗口缓存超出的时候，会写入失败
						break;
					}
					delete buf;	// 提前释放掉
					do_erase = true;
					eraseit = it;
				}
				if (do_erase) {
					wt_bufs.erase(wt_bufs.begin(), eraseit + 1);
				}
				if (!wt_bufs.empty()) {
					// 继续完成未完成的写入事业！
					post_write(nullptr);
				}
			}
			else [[unlikely]] {
				assert(false);
			}
		}
	};

	sstream::sstream(sloop* l, ssocket*s, sstream_opt opt)
	{
		_s = new sstream_t{};
		_s->loop = l;
		_s->sock = s;
		_s->opt = opt;
	}

	sstream::sstream(sstream&& other) noexcept
	{
		_s = std::exchange(other._s, nullptr);
	}

	sstream::~sstream()
	{
		if (_s->op) {
			if (_s->op->cb) {
				_s->op->cb->on_close(this);
			}
			delete _s->op;
		}
		if (_s->sock) {
			delete _s->sock;
		}
		delete _s;
	}

	auto sstream::option() -> const sstream_opt*
	{
		return &_s->opt;
	}

	auto sstream::option() const -> const sstream_opt*
	{
		return &_s->opt;
	}

	auto sstream::loop() -> sloop*
	{
		return _s->loop;
	}

	auto sstream::sock() -> ssocket*
	{
		return _s->sock;
	}

	auto sstream::readbuffer() -> socket_buffer*
	{
		return _s->op->rd_buf;
	}

	auto sstream::writebuffer() -> std::span<socket_buffer*>
	{
		return std::span<socket_buffer*>(_s->op->wt_bufs);
	}

	auto sstream::start_read(sstream_runable* sr) -> void
	{
		assert(_s->op == nullptr);
		_s->op = new sstream_rw_op{};
		_s->op->ss = this->weak_from_this();
		_s->op->cb = sr;
		// this call will fail?
		wsa_attach_iocp(_s->loop, _s->sock, _s->op);
		_s->op->init();
	}

	auto sstream::write(socket_buffer* buf) -> bool
	{
		if (_s->op->stop.load()) {
			return false;
		}
		
		auto self = shared_from_this();
		_s->loop->dispatch(new ioloop_functor([self,this,buf]() {
			_s->op->post_write(buf);
		}));
		return true;
	}

	auto sstream::close() -> void
	{
		bool stop = false;
		if (!_s->op->stop.compare_exchange_strong(stop, true)) {
			return;
		}
		
		auto self = shared_from_this();
		_s->loop->post(new ioloop_functor([self, this]() mutable {
			auto sock = wsa_cast_ssocket(_s->sock);
			::CancelIoEx((HANDLE)sock->s, _s->op->rd_complete);
			_s->op->rd_complete->hold.reset();
			::CancelIoEx((HANDLE)sock->s, _s->op->wt_complete);
			_s->op->wt_complete->hold.reset();
			//self.reset();
		}));
	}

};