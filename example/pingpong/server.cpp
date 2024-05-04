#include <iostream>
#include "shuio/shuio.h"

#include <optional>
#include <memory_resource>

using namespace shu;

class tcp_server {
    int stream_req_;
public:
    struct stream_with_buf {
        std::optional<std::pmr::vector<char>> rd_buf;
        std::optional<std::pmr::vector<char>> wt_buf;
    };

    tcp_server(sloop& loop, addr_storage_t addr) 
    :loop_(loop),stream_req_(0)
    {
        auto ok = server_.start(&loop_, {
            .evClose = [&loop](sacceptor*){
                loop.stop();
            },
            .evConn = [this](sacceptor* a, socket_io_result_t res,
                ssocket* sock,
                addr_pair_t addr) {
            on_client(res, sock, addr);}
        }, addr);
        shu::panic(ok > 0);
    }

    ~tcp_server() {
        std::cout << "tcp server ~" << std::endl;
    }

    void on_client(socket_io_result_t res,
        ssocket* sock,
        addr_pair_t addr) {

        std::unique_ptr<ssocket> ptr(sock);
        if (res.res <= 0) {
            std::cout << "tcp server err:" << strerror(-res.res) << std::endl;
            server_.stop();
            return;
        }

        auto stream_ptr = std::make_unique<sstream>();
        int stream_req = ++stream_req_;
        // fprintf(stdout, "stream new %d \r\n", stream_req);
        stream_ptr->start(&loop_, ptr.release(), {.addr = addr}, {
            .evClose = [stream_req](sstream* s){
                // fprintf(stderr, "stream req:%d ~ \r\n", stream_req);
                delete s;
            }
        });
        auto* rwbuf = shu::set_user_data<stream_with_buf>(*stream_ptr);
        rwbuf->rd_buf.emplace(&pool_);
        rwbuf->wt_buf.emplace(&pool_);
        rwbuf->rd_buf.value().resize(4096);
        
        auto p = stream_ptr.release();
        p->read([this,stream_req](sstream* s, socket_io_result res, buffers_t bufs){
            // fprintf(stdout, "stream on_read %d, %d \r\n", stream_req, res.res);
            on_read(stream_req, s, res, bufs);
        },
        [this](sstream* s, buffer_t& buf){
            auto* rwbuf = shu::get_user_data<stream_with_buf>(*s);
            buf.p = rwbuf->rd_buf.value().data();
            buf.size = rwbuf->rd_buf.value().size();
        });
    }
    
    void on_read(int stream_req, sstream* s, socket_io_result_t res, buffers_t buf) {
        if (res.res <= 0) {
            s->stop();
            return;
        }

        auto* rwbuf = shu::get_user_data<stream_with_buf>(*s);
        // shu::copy_from_buffers(rwbuf->wt_buf.value(), buf);
        rwbuf->wt_buf.value().assign(buf[0].p, buf[0].p + buf[0].size);
        buffer_t wbuf;
        wbuf.p = rwbuf->wt_buf.value().data();
        wbuf.size = res.res;
        s->write(wbuf, [this,stream_req](sstream*s, socket_io_result_t res) mutable {
            // fprintf(stdout, "stream on_write %d, %d \r\n", stream_req, res.res);
            on_write(s,res);
        });
    }

    void on_write(sstream* s, socket_io_result_t res) {

    }
private:
    sloop& loop_;
    sacceptor server_;
    std::pmr::unsynchronized_pool_resource pool_;
};

int main() {
    using namespace shu;

    sloop loop;
    tcp_server server(loop, {8888});
    loop.run();
    return 0;
}