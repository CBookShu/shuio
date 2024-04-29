#include "shuio/shu_stream.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_buffer.h"
#include <vector>
#include <atomic>
#include <deque>


namespace shu {
	struct sstream::sstream_t {
		using iocp_complete_func_t = void(*)(sstream::sstream_t*, OVERLAPPED_ENTRY*);

		struct iocp_complete_ctx : OVERLAPPED {
			iocp_complete_func_t cb;

			iocp_complete_ctx() {
				std::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
			}
		};

		sloop* loop_;
		sstream* owner_;
		std::unique_ptr<ssocket> sock_;
		sstream_opt opt_;
		iocp_complete_ctx reader_;
		iocp_complete_ctx writer_;

		sstream::stream_ctx_t stream_ctx_;
		std::optional<sstream::func_read_t> read_cb_;
		std::optional<sstream::func_write_t> write_cb_;
		
		std::any ud_;

		bool stop_;
		sstream_t(sloop* loop, 
			sstream* owner,
			ssocket* sock, 
			sstream_opt opt, 
			sstream::stream_ctx_t&& stream_event)
			: loop_(loop), owner_(owner),
			sock_(sock),opt_(opt),
			stream_ctx_(std::forward<sstream::stream_ctx_t>(stream_event)) ,
			stop_(false)
		{
			shu::exception_check(!!stream_ctx_.evClose);
		}
		~sstream_t() {
		}

		void start() {
			navite_attach_iocp(loop_, sock_.get());
			navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
				iocp_complete_ctx* obcb = static_cast<iocp_complete_ctx*>(entry->lpOverlapped);
				if (obcb->cb) {
					obcb->cb(this, entry);
				}
			});

			reader_.cb = run_read;
			writer_.cb = run_write;
		}

		bool post_read(std::span<buffer_t> bufs, func_read_t&& cb) {
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			std::vector<WSABUF> wsa_bufs(bufs.size());
			for(int i = 0; i < bufs.size(); ++i) {
				wsa_bufs[i].buf = bufs[i].p;
				wsa_bufs[i].len = bufs[i].size;
			}

			DWORD dwFlags = 0;
			DWORD dwBytes = 0;

			auto r = ::WSARecv(navite_sock->s, wsa_bufs.data(), wsa_bufs.size(), &dwBytes, &dwBytes, &reader_, nullptr);
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					socket_io_result_t res{ .res = -e };
					std::forward<func_read_t>(cb)(res);
					return false;
				}
			}

			read_cb_.emplace(std::forward<func_read_t>(cb));
			return true;
		}
		bool post_write(std::span<buffer_t> bufs, func_write_t&& cb) {
			auto navite_sock = navite_cast_ssocket(sock_.get());
			std::vector<WSABUF> buffs(bufs.size());
			for(int i = 0; i < bufs.size(); ++i) {
				buffs[i].buf = bufs[i].p;
				buffs[i].len = bufs[i].size;
			}

			DWORD dwFlags = 0;
			DWORD dwBytes = 0;
			auto r = ::WSASend(navite_sock->s, buffs.data(), static_cast<DWORD>(buffs.size()), &dwBytes, dwFlags, &writer_, nullptr);
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {

					socket_io_result_t res{ .res = -e };
					std::forward<func_write_t>(cb)(res);
					return false;
				}
			} else {
				socket_io_result_t res{ .res = static_cast<int>(dwBytes)};
				std::forward<func_write_t>(cb)(res);
				return true;
			}

			write_cb_.emplace(std::forward<func_write_t>(cb));
			return true;
		}

		void static run_read(sstream::sstream_t* self, OVERLAPPED_ENTRY* entry) {
			socket_io_result_t res{.res = static_cast<int>(entry->dwNumberOfBytesTransferred)};
			if (entry->dwNumberOfBytesTransferred == 0) {
				// fin?
				res.res = -s_last_error();
			}
			
			{
				auto read_cb = std::move(self->read_cb_.value());
				self->read_cb_.reset();
				read_cb(res);
			}
		}
		
		void static run_write(sstream::sstream_t* self, OVERLAPPED_ENTRY* entry) {
			socket_io_result_t res{.res = static_cast<int>(entry->dwNumberOfBytesTransferred)};
			if (res.res == 0) {
				res.res = -s_last_error();
			}

			{
				auto write_cb = std::move(self->write_cb_.value());
				self->write_cb_.reset();
				write_cb(res);
			}

			if(self->stop_) {
				self->loop_->post([cb = std::move(self->stream_ctx_.evClose), owner = self->owner_](){
					cb(owner);
				});
			}
		}

		void stop() {
			if (std::exchange(stop_, true)) {
				return;
			}

			if (write_cb_) {
				// wait writing
				return;
			}
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			sock_->shutdown(shutdown_type::shutdown_write);
			if (read_cb_) {
				::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &reader_);
			} else {
				loop_->post([cb = std::move(stream_ctx_.evClose), owner = owner_](){
					cb(owner);
				});
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
		shu::exception_check(s_);
		return s_->opt_;
	}

	auto sstream::loop() -> sloop* {
		shu::exception_check(s_);
		return s_->loop_;
	}

	void sstream::start(sloop* l, ssocket* s, sstream_opt opt,
		stream_ctx_t&& stream_event)
	{
		exception_check(!s_);
		l->assert_thread();

		auto ptr = std::make_unique<sstream_t>(l, this, s, opt, std::forward<stream_ctx_t>(stream_event));
		ptr->start();
		s_ = ptr.release();
	}

	auto sstream::set_ud(std::any a) -> std::any* {
		s_->ud_.swap(a);
		return &s_->ud_;
	}
	auto sstream::get_ud() -> std::any* {
		return &s_->ud_;
	}

	bool sstream::read(buffer_t buf, func_read_t&& cb) {
		exception_check(s_);
		buffer_t bufs[1] = {buf};
		return s_->post_read(bufs, std::forward<func_read_t>(cb));
	}

	bool sstream::read(std::span<buffer_t> bufs, func_read_t&& cb) {
		exception_check(s_);
		return s_->post_read(bufs, std::forward<func_read_t>(cb));
	}

	bool sstream::write(buffer_t buf, func_write_t&& cb)
	{
		exception_check(s_);
		buffer_t bufs[1] = {buf};
		return s_->post_write(bufs, std::forward<func_write_t>(cb));
	}

	bool sstream::write(std::span<buffer_t> bufs, func_write_t&& cb) {
		exception_check(s_);
		return s_->post_write(bufs, std::forward<func_write_t>(cb));
	}

	void sstream::stop()
	{
		exception_check(s_);
		s_->stop();
	}

};