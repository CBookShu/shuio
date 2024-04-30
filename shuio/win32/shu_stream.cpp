#include "shuio/shu_stream.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include "shuio/shu_buffer.h"
#include <vector>
#include <atomic>
#include <deque>
#include <map>


namespace shu {
	struct sstream::sstream_t {
		struct iocp_req_t : OVERLAPPED {
			using iocp_complete_func_t = void(*)(sstream::sstream_t*, OVERLAPPED_ENTRY*);
			using ptr = std::unique_ptr<iocp_req_t>;

			int req;
			iocp_complete_func_t func;
			func_op_t complet_cb;
			std::span<buffer_t> bufs;

			iocp_req_t(int req_, iocp_complete_func_t func_, func_op_t&& complet_cb_, std::span<buffer_t>& bufs_)
			: req(req_),func(func_),complet_cb(std::forward<func_op_t>(complet_cb_)),bufs(bufs_)
			{
				shu::zero_mem(static_cast<OVERLAPPED*>(this));
			}
		};
		
		sloop* loop_;
		sstream* owner_;
		std::unique_ptr<ssocket> sock_;
		sstream_opt opt_;
		
		std::uint32_t req_;
		bool reading_;
		std::map<std::uint32_t, std::unique_ptr<iocp_req_t>> readers_;
		bool writting_;
		std::map<std::uint32_t, std::unique_ptr<iocp_req_t>> writers_;

		sstream::stream_ctx_t stream_ctx_;
		std::any ud_;

		bool stop_;
		sstream_t(sloop* loop, sstream* owner,ssocket* sock, sstream_opt opt, sstream::stream_ctx_t&& stream_event)
			: loop_(loop), owner_(owner),
			sock_(sock),opt_(opt),req_(0),
			reading_(false),writting_(false),
			stream_ctx_(std::forward<sstream::stream_ctx_t>(stream_event)) ,
			stop_(false)
		{
			
		}
		~sstream_t() {

		}

		void post_to_close() {
			if (stream_ctx_.evClose) {
				loop_->post([cb = std::move(stream_ctx_.evClose), owner = owner_](){
					cb(owner);
				});
			}
		}

		void start() {
			navite_attach_iocp(loop_, sock_.get());
			navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
				iocp_req_t* obcb = static_cast<iocp_req_t*>(entry->lpOverlapped);
				if (obcb->func) {
					obcb->func(this, entry);
				}
			});
		}

		bool inner_post_read(iocp_req_t* req) {
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			std::vector<WSABUF> wsa_bufs(req->bufs.size());
			for(int i = 0; i < req->bufs.size(); ++i) {
				wsa_bufs[i].buf = req->bufs[i].p;
				wsa_bufs[i].len = req->bufs[i].size;
			}
			DWORD dwFlags = 0;
			DWORD dwBytes = 0;
			auto r = ::WSARecv(navite_sock->s, wsa_bufs.data(), wsa_bufs.size(), &dwBytes, &dwFlags, req, nullptr);
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					socket_io_result_t res{ .res = -e };
					req->complet_cb(res);
					return false;
				}
			}
			return true;
		}

		bool post_read(std::span<buffer_t> bufs, func_op_t&& cb) {
			int req = req_++;
			auto p = std::make_unique<iocp_req_t>(
				req,run_read,std::forward<func_op_t>(cb),
				bufs
			);
			if(readers_.empty()) {
				if (!inner_post_read(p.get())) {
					return false;
				}
				reading_ = true;
			}
			readers_.emplace(req, p.release());
			return true;
		}
		std::tuple<bool, DWORD> inner_post_write(iocp_req_t* req) {
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			std::vector<WSABUF> wsa_bufs(req->bufs.size());
			for(int i = 0; i < req->bufs.size(); ++i) {
				wsa_bufs[i].buf = req->bufs[i].p;
				wsa_bufs[i].len = req->bufs[i].size;
			}
			DWORD dwFlags = 0;
			DWORD dwBytes = 0;
			auto r = ::WSASend(navite_sock->s, wsa_bufs.data(), static_cast<DWORD>(wsa_bufs.size()), &dwBytes, dwFlags, req, nullptr);
			if (r == SOCKET_ERROR) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					socket_io_result_t res{ .res = -e };
					req->complet_cb(res);
					return std::make_tuple(false, 0);
				}
				writting_ = true;
			} else {
				socket_io_result_t res{ .res = static_cast<int>(dwBytes)};
				req->complet_cb(res);
				return std::make_tuple(true, dwBytes);
			}
			return std::make_tuple(true, 0);
		}

		bool post_write(std::span<buffer_t> bufs, func_op_t&& cb) {
			int req = req_++;
			auto p = std::make_unique<iocp_req_t>(
				req,run_write,std::forward<func_op_t>(cb),
				bufs
			);
			if(writers_.empty()) {
				auto tp = inner_post_write(p.get());
				if (!std::get<0>(tp)) {
					return false;
				}
				if (std::get<1>(tp) > 0) {
					return true;
				}
			}
			writers_.emplace(req, p.release());
			return true;
		}

		void static run_read(sstream::sstream_t* self, OVERLAPPED_ENTRY* entry) {
			self->reading_ = false;

			socket_io_result_t res{.res = static_cast<int>(entry->dwNumberOfBytesTransferred)};
			if (entry->dwNumberOfBytesTransferred == 0) {
				// fin?
				res.res = -s_last_error();
			}
			iocp_req_t* req = static_cast<iocp_req_t*>(entry->lpOverlapped);
			{
				auto it = self->readers_.find(req->req);
				shu::panic(it != self->readers_.end());
				auto p = std::move(it->second);
				self->readers_.erase(it);

				p->complet_cb(res);
			}

			while(!self->reading_) {
				if (auto it = self->readers_.begin(); it != self->readers_.end()) {
					if(self->inner_post_read(it->second.get())) {
						break;
					} else {
						self->readers_.erase(it);
					}
				} else {
					break;
				}
			}

			if (self->stop_ && self->writers_.empty() && self->readers_.empty()) {
				self->post_to_close();
			}
		}

		void static run_write(sstream::sstream_t* self, OVERLAPPED_ENTRY* entry) {
			self->writting_ = false;

			socket_io_result_t res{.res = static_cast<int>(entry->dwNumberOfBytesTransferred)};
			if (res.res == 0) {
				res.res = -s_last_error();
			}

			iocp_req_t* req = static_cast<iocp_req_t*>(entry->lpOverlapped);
			{
				auto it = self->writers_.find(req->req);
				shu::panic(it != self->writers_.end());
				auto p = std::move(it->second);
				self->writers_.erase(it);

				p->complet_cb(res);
			}

			while(!self->writting_) {
				if (auto it = self->writers_.begin(); it != self->writers_.end()) {
					auto tp = self->inner_post_write(it->second.get());
					if(std::get<0>(tp)) {
						if (std::get<1>(tp) > 0) {
							it = self->writers_.erase(it);
						} else {
							break;
						}
					} else {
						it = self->writers_.erase(it);
					}
				} else {
					break;
				}
			}

			if (self->stop_ && self->writers_.empty() && self->readers_.empty()) {
				self->post_to_close();
			}
		}

		void stop() {
			if (std::exchange(stop_, true)) {
				return;
			}

			if (!writers_.empty()) {
				// wait writing
				return;
			}
			auto* navite_sock = navite_cast_ssocket(sock_.get());
			sock_->shutdown(shutdown_type::shutdown_write);
			if(readers_.empty()) {
				post_to_close();
			} else {
				// TODO?: ::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &reader_);
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

	void sstream::start(sloop* l, ssocket* s, sstream_opt opt,
		stream_ctx_t&& stream_event)
	{
		panic(!s_);
		l->assert_thread();

		auto ptr = std::make_unique<sstream_t>(l, this, s, opt, std::forward<stream_ctx_t>(stream_event));
		ptr->start();
		s_ = ptr.release();
	}

	auto sstream::get_ud() -> std::any* {
		return &s_->ud_;
	}

	bool sstream::read(buffer_t buf, func_op_t&& cb) {
		panic(s_);
		buffer_t bufs[1] = {buf};
		return s_->post_read(bufs, std::forward<func_op_t>(cb));
	}

	bool sstream::read(std::span<buffer_t> bufs, func_op_t&& cb) {
		panic(s_);
		return s_->post_read(bufs, std::forward<func_op_t>(cb));
	}

	bool sstream::write(buffer_t buf, func_op_t&& cb)
	{
		panic(s_);
		buffer_t bufs[1] = {buf};
		return s_->post_write(bufs, std::forward<func_op_t>(cb));
	}

	bool sstream::write(std::span<buffer_t> bufs, func_op_t&& cb) {
		panic(s_);
		return s_->post_write(bufs, std::forward<func_op_t>(cb));
	}

	void sstream::stop()
	{
		panic(s_);
		s_->stop();
	}

};