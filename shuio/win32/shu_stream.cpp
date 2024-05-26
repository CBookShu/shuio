#include "shuio/shu_stream.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_buffer.h"
#include <vector>
#include <map>

namespace shu {
	static const char zero_buf_[] = "";
	struct sstream::sstream_t {
		using iocp_complete_func_t = void(*)(sstream::sstream_t*, OVERLAPPED_ENTRY*);
		struct iocp_req_write_t : OVERLAPPED {
			iocp_complete_func_t func;
			func_on_write_t on_write_cb;
			bool running;

			iocp_req_write_t(iocp_complete_func_t func_)
			: func(func_),running(false){}

			void zero_op() {
				shu::zero_mem(static_cast<OVERLAPPED&>(*this));
			}
			void set(func_on_write_t&& cb) {
				on_write_cb = std::forward<func_on_write_t>(cb);
			}
		};
		
		struct iocp_req_read_t : OVERLAPPED {
			iocp_complete_func_t func;
			func_on_read_t on_read_cb;
			func_alloc_t on_alloc_cb;
			bool running;

			iocp_req_read_t(iocp_complete_func_t func_)
			: func(func_),running(false){}

			void zero_op() {
				shu::zero_mem(static_cast<OVERLAPPED&>(*this));
			}
			void set(func_on_read_t&& cb, func_alloc_t&& alloc) {
				on_read_cb = std::forward<func_on_read_t>(cb);
				on_alloc_cb = std::forward<func_alloc_t>(alloc);
			}
		};

		sloop* loop_;
		sstream* owner_;
		std::unique_ptr<ssocket> sock_;
		sstream_opt opt_;
		
		iocp_req_read_t reader_;
		iocp_req_write_t writer_;

		sstream::stream_ctx_t stream_ctx_;
		std::any ud_;

		bool stop_;
		bool close_;
		sstream_t(sloop* loop, sstream* owner,UPtr<ssocket> sock, sstream_opt opt, sstream::stream_ctx_t&& stream_event)
			: loop_(loop), owner_(owner),
			sock_(std::move(sock)),opt_(opt),
			reader_(run_read),writer_(run_write),
			stream_ctx_(std::forward<sstream::stream_ctx_t>(stream_event)) ,
			stop_(false),close_(false)
		{
		}
		~sstream_t() {

		}

		void post_to_close() {
			if (std::exchange(close_, true)) {
				return;
			}
			if (stream_ctx_.evClose) {
				loop_->post([cb = std::move(stream_ctx_.evClose), owner = owner_](){
					cb(owner);
				});
			}
		}

		int start() {
			navite_attach_iocp(loop_, sock_.get());
			navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
				iocp_req_write_t* obcb = static_cast<iocp_req_write_t*>(entry->lpOverlapped);
				if (obcb->func) {
					obcb->func(this, entry);
				}
			});
			return 1;
		}

		int init_read(func_on_read_t&& cb, func_alloc_t&& alloc) {
			shu::panic(!reader_.running);
			reader_.set(std::forward<func_on_read_t>(cb), std::forward<func_alloc_t>(alloc));
			int err = sock_->noblock(true);
			if (err <= 0) {
				socket_io_result res{err};	
				reader_.on_read_cb(owner_, res, buffers_t{});
				return err;
			}

			err = sock_->nodelay(true);
			if (err <= 0) {
				socket_io_result res{err};	
				reader_.on_read_cb(owner_, res, buffers_t{});
				return err;
			}
			return post_read();
		}

		int post_read() {
			if (reader_.running) {
				return 0;
			}
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			DWORD dwFlags = 0;
			DWORD dwBytes = 0;
			WSABUF buf;
			buf.buf = (char*)&zero_buf_;
			buf.len = 0;
			reader_.zero_op();
			int r = ::WSARecv(navite_sock->s, &buf, 1, &dwBytes, &dwFlags, &reader_, nullptr);

			reader_.running = true;
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					reader_.running = false;

					socket_io_result_t res{ .res = -e };
					reader_.on_read_cb(owner_, res, buffers_t{});
					return -e;
				}
			} else {
				OVERLAPPED_ENTRY entry;
				entry.dwNumberOfBytesTransferred = dwBytes;
				entry.Internal = 0;
				entry.lpCompletionKey = reinterpret_cast<ULONG_PTR>(&navite_sock->tag);
				entry.lpOverlapped = &reader_;
				run_read(this, &entry);
				return 1;
			}
			return 1;
		}

		bool post_write(buffers_t bufs, func_on_write_t&& cb) {
			if (writer_.running) {
				return false;
			}
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			DWORD dwFlags = 0;
			DWORD dwBytes = 0;
			writer_.zero_op();
			int r = ::WSASend(navite_sock->s, reinterpret_cast<WSABUF*>(bufs.data()), static_cast<DWORD>(bufs.size()), &dwBytes, dwFlags, &writer_, nullptr);
			
			writer_.running = true;
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					writer_.running = false;

					socket_io_result_t res{ .res = -e };
					std::forward<func_on_write_t>(cb)(owner_, res);
					return false;
				}
			} else {
				writer_.set(std::forward<func_on_write_t>(cb));

				OVERLAPPED_ENTRY entry;
				entry.dwNumberOfBytesTransferred = dwBytes;
				entry.Internal = 0;
				entry.lpCompletionKey = reinterpret_cast<ULONG_PTR>(&navite_sock->tag);
				entry.lpOverlapped = &writer_;
				run_write(this, &entry);
				return true;
			}
			return true;
		}

		
		void static run_read(sstream::sstream_t* self, OVERLAPPED_ENTRY* entry) {
			self->reader_.running = false;
			socket_io_result_t res;
			
			char buffer_stack[65535];
			buffer_t buf[2];
			shu::zero_mem(buf[0]);
			if(self->reader_.on_alloc_cb) {
				self->reader_.on_alloc_cb(self->owner_, buf[0]);
			}
			buf[1].p = buffer_stack;
			buf[1].size = sizeof(buffer_stack);

			DWORD dwFlags = 0;
			DWORD dwBytes = 0;
			auto* navite_sock = navite_cast_ssocket(self->sock_.get());
			auto r = ::WSARecv(navite_sock->s, reinterpret_cast<WSABUF*>(buf), 2, &dwBytes, &dwFlags, nullptr, nullptr);
			res.res = dwBytes;
			if (res.res == 0) {
				res.res = -s_last_error();
			}

			if (res.res > 0) {
				auto diff = res.res - static_cast<int>(buf[0].size);
				if (diff >= 0) {
					buf[1].size = diff;
				} else {
					buf[1].size = 0;
					buf[0].size = res.res;
				}
			}
			self->reader_.on_read_cb(self->owner_, res, buf);

			if (self->stop_) {
				if(!self->writer_.running) {
					self->post_to_close();
				}
			} else {
				self->post_read();
			}
		}

		void static run_write(sstream::sstream_t* self, OVERLAPPED_ENTRY* entry) {
			self->writer_.running = false;
			socket_io_result_t res{.res = static_cast<int>(entry->dwNumberOfBytesTransferred)};
			if (entry->dwNumberOfBytesTransferred == 0) {
				// fin?
				res.res = -s_last_error();
			}
			
			self->writer_.on_write_cb(self->owner_, res);

			if (self->stop_
			&& !self->reader_.running 
			&& !self->writer_.running ) {
				self->post_to_close();
			}
		}

		void stop() {
			if (std::exchange(stop_, true)) {
				return;
			}

			if(writer_.running) {
				// wait writing
				return;
			}
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			if(!reader_.running) {
				post_to_close();
			} else {
				// 有时候它并不起作用。。。
				::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &reader_);

				// 上面不起作用没关系，这里close 一定让它不能继续操作了
				sock_->close();

				// 下一帧强制释放自己
				post_to_close();
			}
		}
	};
	
	sstream::sstream():s_(nullptr)
	{
		
	}

	sstream::sstream(sstream&& other) noexcept
	{
		s_ = std::exchange(other.s_, nullptr);
	}

	sstream::~sstream()
	{
		if (s_) {
			delete s_;
		}
	}

	auto sstream::option() -> sstream_opt {
		shu::panic(s_);
		return s_->opt_;
	}

	auto sstream::loop() -> sloop* {
		shu::panic(s_);
		return s_->loop_;
	}

	int sstream::start(sloop* l, UPtr<ssocket> sock, sstream_opt opt,
		stream_ctx_t&& stream_event)
	{
		panic(!s_);
		l->assert_thread();

		s_ = new sstream_t(l, this, std::move(sock), opt, std::forward<stream_ctx_t>(stream_event));
		int r = s_->start();
		return r;
	}

	int sstream::read(func_on_read_t&& cb, func_alloc_t&& alloc) {
		shu::panic(s_);
		return s_->init_read(std::forward<func_on_read_t>(cb), std::forward<func_alloc_t>(alloc));
	}

	auto sstream::get_ud() -> std::any* {
		return &s_->ud_;
	}

	bool sstream::write(buffer_t buf, func_on_write_t&& cb)
	{
		panic(s_);
		buffer_t bufs[1] = {buf};
		return s_->post_write(bufs, std::forward<func_on_write_t>(cb));
	}

	bool sstream::write(buffers_t bufs, func_on_write_t&& cb) {
		panic(s_);
		return s_->post_write(bufs, std::forward<func_on_write_t>(cb));
	}

	void sstream::stop()
	{
		panic(s_);
		s_->stop();
	}

};