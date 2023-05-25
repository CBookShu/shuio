#include "shuio/shu_server.h"
#include "linux_detail.h"
#include "shuio/shu_socket.h"

#include <netinet/in.h>
#include <arpa/inet.h>

namespace shu {
    struct acceptor;
    struct sserver::sserver_t {
		sloop* loop;
		ssocket* sock;
		sserver_opt opt;
		acceptor* accept;
    };

    struct acceptor : uring_callback {
        uring_ud_io_t complete;
        struct sockaddr_in addr_in_client;
        socklen_t addr_size = sizeof(addr_in_client);
        sserver_runnable* user_callback;
        sserver* server;
        
        void init() {
            complete.type = op_type::type_io;
            complete.cb = this;
            post_accept();
        }

        void post_accept() {
            std::memset(&addr_in_client, 0, sizeof(addr_in_client));
            io_uring_push_sqe(server->loop(), [&](io_uring* ring){
                auto* navie_sock = navite_cast_ssocket(server->sock());
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                io_uring_prep_accept(sqe, navie_sock->fd, (struct sockaddr*)&addr_in_client, &addr_size, 0);
                io_uring_sqe_set_data(sqe, &complete);
                io_uring_submit(ring);
            });
        }

        void run(io_uring_cqe* cqe) noexcept override {
            // 有链接过来
            socket_io_result_t res{ .err = 0};
            ssocket* sock = nullptr;
            if(cqe->res >= 0) {
                // 有效值
                sock = new ssocket({});
                auto* navie_sock = navite_cast_ssocket(sock);
                navie_sock->fd = cqe->res;
            } else {
                res.err = 1;
                res.naviteerr = cqe->res;
            }
            addr_pair_t addr;
			addr.remote.port = ntohs(addr_in_client.sin_port);
			inet_ntop(AF_INET, &addr_in_client.sin_addr, addr.remote.ip, std::size(addr.remote.ip)-1);
			addr.remote.iptype = server->option()->addr.iptype;

			addr.local = server->option()->addr;
            user_callback->new_client(res, server, sock, addr);

            post_accept();
        }

    };

    sserver::sserver(sserver_opt opt) {
        _s = new sserver_t{};
        _s->opt = opt;
    }

    sserver::sserver(sserver&& other) noexcept
	{
		_s = std::exchange(other._s, nullptr);
	}

    sserver::~sserver() {
        if(!_s) return;
        if(_s->sock) {
            delete _s->sock;
        }
        if(_s->accept) {
            if(_s->accept->user_callback) {
                _s->accept->user_callback->destroy();
            }
            delete _s->accept;
        }
    }

    auto sserver::option() -> const sserver_opt* {
        return &_s->opt;
    }

    auto sserver::loop() -> sloop* {
        return _s->loop;
    }

    auto sserver::sock() -> ssocket* {
        return _s->sock;
    }

    auto sserver::start(sloop* loop, sserver_runnable* r,addr_storage_t addr) -> bool {
        _s->loop = loop;
        _s->sock = new ssocket({});
        _s->sock->init(addr.iptype);
		_s->sock->reuse_addr(true);
		// _s->sock->noblock(true);
        _s->accept = new acceptor{};
        _s->accept->server = this;
        _s->accept->user_callback = r;
        _s->opt.addr = addr;
        
        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(addr.port);
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        if(::inet_pton(AF_INET, addr.ip, &(serv_addr.sin_addr)) != 1) {
            perror("addr error");
            exit(1);
        }

        auto* navie_sock = navite_cast_ssocket(_s->sock);
        if(::bind(navie_sock->fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("bind error");
            exit(1);
        }

        if(::listen(navie_sock->fd, 512) < 0) {
            perror("listen error");
            exit(1);
        }
        _s->accept->init();
    }
};