#include "shuio/shu_server.h"
#include "win32_detail.h"
#include "shuio/shu_loop.h"
#include "shuio/shu_socket.h"
#include <cassert>

namespace shu {

	struct socket_accptor_op;
	struct sserver::sserver_t {
		sloop* loop;
		ssocket* sock;
		sserver_opt opt;
		socket_accptor_op* op;
	};

	struct acceptor_complete_t : OVERLAPPED {
		enum { buffer_size = (sizeof(sockaddr) + 16) * 2 };
		ssocket* sock;
		char buffer[buffer_size];
		~acceptor_complete_t() {
			if (sock) {
				delete sock;
			}
		}
	};

	struct socket_accptor_op : iocp_sock_callback {
		sserver_runnable* creator;
		LPFN_ACCEPTEX lpfnAcceptEx;
		LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs;
		sserver* server = nullptr;
		acceptor_complete_t* completes[4] = {};

		socket_accptor_op(sserver* p) :server(p) {}
		~socket_accptor_op() {
			for (std::size_t i = 0; i < std::size(completes); ++i) {
				delete completes[i];
			}
		}

		void init() {
			wsa_attach_iocp(server->loop(), server->sock(), this);

			for (std::size_t i = 0; i < std::size(completes); ++i) {
				completes[i] = new acceptor_complete_t{};
				post_acceptor(completes[i]);
			}
		}

		void post_acceptor(acceptor_complete_t* complete) {
			// iocp 可以支持一次性投递多个acceptor，这里的示范做了4个！
			complete->sock = new ssocket({});
			complete->sock->init(server->option()->addr.iptype);
			
			auto* client_sock = wsa_cast_ssocket(complete->sock);
			auto* server_sock = wsa_cast_ssocket(server->sock());
			DWORD bytes_read = 0;
			auto r = lpfnAcceptEx(server_sock->s, client_sock->s, complete->buffer, 0, sizeof(sockaddr) + 16, sizeof(sockaddr) + 16, &bytes_read, complete);
			if (!r) {
				auto e = WSAGetLastError();
				assert(e == ERROR_IO_PENDING);
				if (e != ERROR_IO_PENDING) {
					// TODO: 一旦走到这里，就代表重大故障！考虑是否增加重试?
					complete->sock;
					complete = nullptr;
					socket_io_result_t res{ .err = 1, .naviteerr = s_last_error() };
					creator->new_client(res, server, nullptr, {});
				}
			}
		}
		virtual void run(OVERLAPPED_ENTRY* entry) noexcept override {
			auto* complete = static_cast<struct acceptor_complete_t*>(entry->lpOverlapped);

			SOCKADDR_IN* ClientAddr = NULL;
			SOCKADDR_IN* LocalAddr = NULL;
			int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);

			// TODO: 这里可以优化将第一个包和accept一并获取到！
			// 我就喜欢这样的api，没有返回值！ 一定成功！
			lpfnGetAcceptExSockAddrs(complete->buffer, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				(LPSOCKADDR*)&LocalAddr, &localLen,
				(LPSOCKADDR*)&ClientAddr, &remoteLen);
			auto* client = complete->sock;

			addr_pair_t addr{};
			addr.remote.port = ntohs(ClientAddr->sin_port);
			inet_ntop(AF_INET, &ClientAddr->sin_addr, addr.remote.ip, std::size(addr.remote.ip)-1);
			addr.remote.iptype = server->option()->addr.iptype;

			addr.local.port = ntohs(LocalAddr->sin_port);
			inet_ntop(AF_INET, &LocalAddr->sin_addr, addr.local.ip, std::size(addr.local.ip) - 1);
			addr.local.iptype = server->option()->addr.iptype;

			socket_io_result_t res{ .err = 0 };
			creator->new_client(res, server, client, addr);
			post_acceptor(complete);
		}
	};

	sserver::sserver(sserver_opt opt)
	{
		_s = new sserver_t{};
		_s->opt = opt;
	}

	sserver::sserver(sserver&& other) noexcept
	{
		_s = std::exchange(other._s, nullptr);
	}

	sserver::~sserver()
	{
		if (_s->sock) {
			delete _s->sock;
		}
		if (_s->op) {
			delete _s->op;
		}
		delete _s;
	}

	auto sserver::option() -> const sserver_opt*
	{
		return &_s->opt;
	}

	auto sserver::loop() -> sloop*
	{
		return _s->loop;
	}

	auto sserver::sock() -> ssocket*
	{
		return _s->sock;
	}

	auto sserver::start(sloop* loop, sserver_runnable* creator, addr_storage_t addr) -> bool
	{
		_s->loop = loop;
		_s->opt.addr = addr;
		_s->sock = new ssocket({});
		_s->sock->init(addr.iptype);
		_s->sock->reuse_addr(true);
		_s->sock->noblock(true);
		_s->op = new socket_accptor_op{ this };
		_s->op->creator = creator;

		auto* server_sock = wsa_cast_ssocket(_s->sock);
		DWORD dwBytes;
		GUID GuidAcceptEx = WSAID_ACCEPTEX;
		auto iResult = WSAIoctl(server_sock->s, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&GuidAcceptEx, sizeof(GuidAcceptEx),
			&_s->op->lpfnAcceptEx, sizeof(_s->op->lpfnAcceptEx),
			&dwBytes, NULL, NULL);
		assert(iResult != SOCKET_ERROR);

		GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
		iResult = WSAIoctl(server_sock->s, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&GuidGetAcceptExSockAddrs, sizeof(GuidGetAcceptExSockAddrs),
			&_s->op->lpfnGetAcceptExSockAddrs, sizeof(&_s->op->lpfnGetAcceptExSockAddrs),
			&dwBytes, NULL, NULL);
		assert(iResult != SOCKET_ERROR);

		// bind,listen
		sockaddr_in addr_in{};
		addr_in.sin_family = AF_INET;
		addr_in.sin_port = htons(addr.port);
		if (std::strlen(addr.ip) == 0) {
			addr_in.sin_addr = in4addr_any;
		}
		else {
			auto ret = ::inet_pton(AF_INET, addr.ip, &addr_in.sin_addr);
			if (ret != 1) {
				return false;
			}
		}

		auto ret = ::bind(server_sock->s, (struct sockaddr*)&addr_in, sizeof(addr_in));
		if (ret == SOCKET_ERROR) {
			auto e = s_last_error();
			return false;
		}

		if (_s->sock->option()->flags.udp) [[unlikely]] {
			return true;
		}
		ret = ::listen(server_sock->s, SOMAXCONN);
		if (ret == SOCKET_ERROR) {
			return false;
		}

		// 如果是udp 那就不需要accept,直接stream获取数据了
		_s->op->init();
		return true;
	}

};