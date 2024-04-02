#include "shuio/shu_stream.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_buffer.h"
#include <vector>
#include <atomic>

namespace shu {
	struct OpWithCb : OVERLAPPED {
		std::function<void(OVERLAPPED_ENTRY*)> cb;

		OpWithCb() {
			std::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
		}
	};

	struct sstream::sstream_t : public std::enable_shared_from_this<sstream_t> {
		sloop* loop_;
		std::unique_ptr<ssocket> sock_;
		sstream_opt opt_;
		OpWithCb reader_;
		OpWithCb writer_;

		std::unique_ptr<socket_buffer> read_buffer_;
		std::vector<socket_buffer> write_buffer_;

		sstream::func_read_t rd_cb_;
		sstream::func_write_t wt_cb_;

		std::shared_ptr<sstream_t> read_hold_;
		std::shared_ptr<sstream_t> write_hold_;

		bool stopping;

		sstream_t(sloop* loop, 
			ssocket* sock, 
			sstream_opt opt, 
			sstream::func_read_t&& rd_cb, 
			sstream::func_write_t&& wt_cb) {
			loop_ = loop;
			sock_.reset(sock);
			opt_ = opt;

			reader_ = {};
			writer_ = {};

			read_buffer_ = std::make_unique<socket_buffer>(opt.read_buffer_init);

			rd_cb_ = std::forward<sstream::func_read_t>(rd_cb);
			wt_cb_ = std::forward<sstream::func_write_t>(wt_cb);

			stopping = false;
		}
		void start() {
			navite_attach_iocp(loop_, sock_.get());
			navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
				OpWithCb* obcb = static_cast<OpWithCb*>(entry->lpOverlapped);
				if (obcb->cb) {
					obcb->cb(entry);
				}
			});

			reader_.cb = [this](OVERLAPPED_ENTRY* entry) {
				run_read(entry);
			};
			writer_.cb = [this](OVERLAPPED_ENTRY* entry) {
				run_write(entry);
			};

			post_read();
		}
		void post_read() {
			if (stopping) {
				return;
			}
			assert(!read_hold_);

			auto* navite_sock = navite_cast_ssocket(sock_.get());
			auto s = read_buffer_->prepare(opt_.read_buffer_count_per_op);
			WSABUF buf = { s.size_bytes(), s.data() };
			DWORD dwFlags = 0;
			DWORD dwBytes = 0;

			auto r = ::WSARecv(navite_sock->s, &buf, 1, &dwBytes, &dwBytes, &reader_, nullptr);
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					socket_io_result_t res{ .bytes = 0,.err = -1, .naviteerr = e };
					read_ctx_t ctx{ .buf = *read_buffer_};
					rd_cb_(res, ctx);
					return;
				}
			}

			read_hold_ = shared_from_this();
		}
		void post_write(socket_buffer* buf = nullptr) {
			if (stopping) {
				// 调用了stop后，不再接受write
				return;
			}

			if (buf && !buf->ready().empty()) {
				write_buffer_.emplace_back(std::move(*buf));
			}
			
			if (write_hold_) {
				// writing
				return;
			}

			auto navite_sock = navite_cast_ssocket(sock_.get());
			std::vector<WSABUF> buffs; buffs.reserve((write_buffer_.size()));
			for (auto& it : write_buffer_) {
				auto data = it.ready();
				WSABUF buf;
				buf.buf = data.data();
				buf.len = data.size_bytes();
				buffs.emplace_back(buf);
			}
			DWORD dwFlags = 0;
			DWORD dwBytes = 0;
			auto r = ::WSASend(navite_sock->s, buffs.data(), buffs.size(), &dwBytes, dwFlags, &writer_, nullptr);
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					socket_io_result_t res{ .bytes = 0,.err = -1, .naviteerr = e };
					write_ctx_t ctx{ .bufs = write_buffer_, };
					wt_cb_(res, ctx);
					return;
				}
			}

			write_hold_ = shared_from_this();
		}

		void run_read(OVERLAPPED_ENTRY* entry) {
			auto self = shared_from_this();
			read_hold_.reset();

			socket_io_result_t res{ 
				.bytes = entry->dwNumberOfBytesTransferred,
				.err = 0,
				.naviteerr = s_last_error() };
			read_ctx_t ctx{ .buf = *read_buffer_ };
			if (entry->dwNumberOfBytesTransferred == 0) {
				// fin
				res.err = -1;
				rd_cb_(res, ctx);
				return;
			}
			
			// 回调
			read_buffer_->commit(entry->dwNumberOfBytesTransferred);
			rd_cb_(res, ctx);

			// 继续读
			post_read();
		}
		void run_write(OVERLAPPED_ENTRY* entry) {
			auto self = shared_from_this();
			write_hold_.reset();

			socket_io_result_t res{
				.bytes = entry->dwNumberOfBytesTransferred,
				.err = 0,
				.naviteerr = s_last_error() };
			write_ctx_t ctx{ .bufs = write_buffer_ };
			if (entry->dwNumberOfBytesTransferred == 0) {
				res.err = -1;
				wt_cb_(res, ctx);
				return;
			}

			wt_cb_(res, ctx);

			auto total = entry->dwNumberOfBytesTransferred;
			auto it = write_buffer_.begin();
			for (; it != write_buffer_.end(); ++it) {
				total -= it->consume(total);
				auto ready = it->ready();
				if (ready.size() > 0) {
					break;
				}
			}
			if (it != write_buffer_.begin()) {
				write_buffer_.erase(write_buffer_.begin(), it);
			}

			if (!write_buffer_.empty()) {
				post_write();
			}
		}

		void stop() {
			if (std::exchange(stopping, true)) {
				return;
			}

			/*
			这里仅仅通过CancelIoEx的方式，最终析构，closesocket，可以最大化
			接收iocp中已完成的内容；
			如果在这里直接立刻执行closesocket，那么会立即终止后续的读写
			*/ 
			
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &reader_);
			::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &writer_);
			sock_->close();
		}
	};
	
	sstream::sstream()
	{

	}

	sstream::sstream(sstream&& other) noexcept
	{
		s_.swap(other.s_);
	}

	sstream::~sstream()
	{
		if (auto sptr = s_.lock()) {

		}
	}

	void sstream::start(sloop* l, ssocket* s, sstream_opt opt,
		sstream::func_read_t&& rcb, sstream::func_write_t&& wcb)
	{
		auto sptr = std::make_shared<sstream::sstream_t>(l, s, opt, std::forward<func_read_t>(rcb), std::forward<func_write_t>(wcb));
		s_ = sptr;

		l->dispatch([sptr]() {
			sptr->start();
		});
	}

	void sstream::write(socket_buffer&& buf)
	{
		if (auto sptr = s_.lock()) {
			sptr->loop_->dispatch([sptr, buf = { std::move(buf) }]() mutable {
				sptr->post_write(const_cast<socket_buffer*>(buf.begin()));
			});
		}
	}

	void sstream::stop()
	{
		if (auto sptr = s_.lock()) {
			sptr->loop_->dispatch([sptr]() {
				sptr->stop();
			});
		}
	}

};