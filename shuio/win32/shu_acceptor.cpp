#include "shuio/shu_acceptor.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include <cassert>

namespace shu {

	struct sacceptor::sacceptor_t {
		struct acceptor_complete_t : OVERLAPPED {
			enum { buffer_size = (sizeof(sockaddr_storage) + 16) * 2 };

			std::unique_ptr<ssocket> sock_;
			char buffer_[buffer_size];

			bool doing_;

			acceptor_complete_t()
			: doing_(false)
			{
				shu::zero_mem(static_cast<OVERLAPPED&>(*this));
				shu::zero_mem(buffer_);
			}
		};
		
		enum {
			max_acceptor_post = 32
		};

		sloop* loop_;
		sacceptor* owner_;
		std::unique_ptr<ssocket> sock_;
		addr_pair_t addr_pair_;
		sacceptor::event_ctx server_ctx_;
		std::vector<acceptor_complete_t> accept_ops;
		bool stop_;
		bool close_;

		sacceptor_t(sloop* loop, sacceptor* owner, event_ctx&& server_ctx, addr_storage_t addr)
		: loop_(loop), owner_(owner), addr_pair_{.local = addr},
		server_ctx_(std::forward<event_ctx>(server_ctx)), stop_(false),close_(false)
		{
			shu::panic(!!server_ctx_.evConn);
		}
		~sacceptor_t() {
			
		}

		void post_close() {
			if (std::exchange(close_, true)) {
				return;
			}
			if (server_ctx_.evClose) {
				loop_->post([cb = std::move(server_ctx_.evClose), owner = owner_](){
					cb(owner);
				});
			}
		}

		int start() {
			// 先创建 sock和对应的bind和listen
			addrinfo hints { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
            addrinfo *server_info {nullptr};
			std::string_view ip(addr_pair_.local.ip.data());
			std::string service = std::to_string(addr_pair_.local.port);
			int rv = getaddrinfo(ip.data(), service.c_str(), &hints, &server_info);
            if(rv != 0) {
				socket_io_result_t res{ .res = -s_last_error() };
				server_ctx_.evConn(res, nullptr, addr_pair_);
				return res.res;
            }
            auto f_deleter = [](addrinfo* p){ freeaddrinfo(p);};
            std::unique_ptr<addrinfo, void(*)(addrinfo*)> ptr(server_info, f_deleter);
			int last_erro;
            for (auto p = server_info; p != nullptr; p = p->ai_next) {
                std::unique_ptr<ssocket> ptr_sock = std::make_unique<ssocket>();

                ptr_sock->init(p->ai_socktype == SOCK_DGRAM, p->ai_family ==AF_INET6);
                if (!ptr_sock->bind(p->ai_addr, p->ai_addrlen)) {
					last_erro = s_last_error();
                    continue;
                }
				
				if(!ptr_sock->listen()) {
					last_erro = s_last_error();
					continue;
				}

				addr_pair_.local.family = p->ai_family;
				sock_.swap(ptr_sock);
                break;
            }

			if(!sock_) {
				socket_io_result_t res{ .res = -last_erro };
				server_ctx_.evConn(res, nullptr, addr_pair_);
				return res.res;
			}

			sock_->reuse_addr(true);
			sock_->noblock(true);

			// 绑定IOCP 并开始post accept
			navite_attach_iocp(loop_, sock_.get());
			navite_sock_setcallbak(sock_.get(), [this](OVERLAPPED_ENTRY* entry) {
				run(entry);
			});

			accept_ops.resize(max_acceptor_post);
			int doing_count = 0;
			for (auto& op:accept_ops) {
				last_erro = post_acceptor(&op);
				if (last_erro <= 0) {
					break;
				}
			}

			return last_erro;
		}

		int post_acceptor(acceptor_complete_t* acceptor) {
			if(stop_) {
				return 0;
			}
			acceptor->sock_ = std::make_unique<ssocket>();
			acceptor->sock_->init(false, addr_pair_.local.family == AF_INET6);

			auto* client_sock = navite_cast_ssocket(acceptor->sock_.get());
			auto* server_sock = navite_cast_ssocket(sock_.get());
			DWORD bytes_read = 0;
			auto r = win32_extension_fns::AcceptEx(
				server_sock->s,
				client_sock->s,
				acceptor->buffer_, 0,
				sizeof(sockaddr) + 16, sizeof(sockaddr) + 16,
				&bytes_read, acceptor);

			if (!r) {
				auto e = s_last_error();
				if (e != WSA_IO_PENDING) {
					socket_io_result_t res{ .res = -e };
					server_ctx_.evConn(res, nullptr, addr_pair_);
					return res.res;
				}
			}
			acceptor->doing_ = true;
			return 1;
		}

		void run(OVERLAPPED_ENTRY* entry) {
			auto* op = static_cast<struct acceptor_complete_t*>(entry->lpOverlapped);
			op->doing_ = false;

			addr_pair_t addr{};
			sockaddr_storage* client_addr = nullptr;
			sockaddr_storage* local_addr = nullptr;
			int remoteLen = 0;
			int localLen = 0;
			if (addr_pair_.local.family == AF_INET) {
				remoteLen = sizeof(sockaddr_in);
				localLen = sizeof(sockaddr_in);
			} else {
				remoteLen = sizeof(sockaddr_in6);
				localLen = sizeof(sockaddr_in6);
			}
			win32_extension_fns::GetAcceptExSockaddrs(op->buffer_, 0,
					localLen + 16, remoteLen + 16,
					(sockaddr**)&local_addr, &localLen,
					(sockaddr**)&client_addr, &remoteLen);
			
			if (localLen == 0 || remoteLen == 0) {

			} else {
				shu::sockaddr_2_storage(client_addr, &addr.remote);
				shu::sockaddr_2_storage(local_addr, &addr.local);
			}
			
			socket_io_result_t res{ .res = 1 };
			if(addr.local.port != addr_pair_.local.port) {
				// err
				res.res = -s_last_error();
				op->sock_.reset();
			}

			server_ctx_.evConn(res, op->sock_.release(), addr);

			if (stop_) {
				int left = 0;
				for(auto& op:accept_ops) {
					if (op.doing_) {
						left++;
					}
				}
				if(left == 0) {
					post_close();
				}
			}
			else if(post_acceptor(op) <= 0) {
				// post_acceptor err will call callback
			}
		}
	
		void stop() {
			if (std::exchange(stop_, true)) {
				return;
			}

			auto* navite_sock = navite_cast_ssocket(sock_.get());
			bool cancel = false;
			for (auto& op : accept_ops) {
				if(op.doing_) {
					::CancelIoEx(reinterpret_cast<HANDLE>(navite_sock->s), &op);
					op.sock_.reset();
					cancel = true;
				}
			}
			if (!cancel) {
				post_close();
			}
		}
	};

	sacceptor::sacceptor():s_(nullptr)
	{
	}

	sacceptor::sacceptor(sacceptor&& other) noexcept
	{
		s_ = std::exchange(other.s_, nullptr);
	}

	sacceptor::~sacceptor()
	{
		if(s_) {
			delete s_;
		}
	}

	int sacceptor::start(sloop* loop, event_ctx&& ctx,addr_storage_t addr)
	{
		shu::panic(!s_);
		s_ = new sacceptor_t(loop, this, std::forward<event_ctx>(ctx), addr);
		auto r = s_->start();
		return r;
	}

	auto sacceptor::loop() -> sloop* {
		shu::panic(s_);
		return s_->loop_;
	}

	auto sacceptor::addr() -> const addr_storage_t& {
		shu::panic(s_);
		return s_->addr_pair_.local;
	}

	void sacceptor::stop()
	{
		shu::panic(s_);
		s_->stop();
	}

};