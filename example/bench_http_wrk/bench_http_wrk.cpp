#include <iostream>
#include "shuio/shuio.h"

#include <optional>
#include <memory_resource>

using namespace shu;

constexpr std::string_view response = R"(
HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Content-Length: 13

Hello, World!
)";

class tcp_server {
    int stream_req_;
public:
    tcp_server(sloop& loop, addr_storage_t addr) 
    :loop_(loop),stream_req_(0),servererr_(false)
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
            printf("tcp server err:%d,%s \r\n", res.res, strerror(-res.res));
            servererr_ = true;
            for(auto& con:cons_) {
                con.second->stop();
            }
            return;
        }
        servererr_ = true;
        auto stream_ptr = std::make_unique<sstream>();
        int stream_req = ++stream_req_;
        // fprintf(stdout, "stream new %d \r\n", stream_req);
        stream_ptr->start(&loop_, ptr.release(), {.addr = addr}, {
            .evClose = [stream_req, this](sstream* s){
                // fprintf(stderr, "stream req:%d ~ \r\n", stream_req);
                delete s;

                this->cons_.erase(stream_req);
                if (this->cons_.empty() && this->servererr_) {
                    this->server_.stop();
                }
            }
        });
        
        auto p = stream_ptr.release();
        p->read([this,stream_req](sstream* s, socket_io_result res, buffers_t bufs){
            // fprintf(stdout, "stream on_read %d, %d \r\n", stream_req, res.res);
            on_read(stream_req, s, res, bufs);
        });

        cons_[stream_req] = p;
    }
    
    void on_read(int stream_req, sstream* s, socket_io_result_t res, buffers_t buf) {
        if (res.res <= 0) {
            s->stop();
            return;
        }

        // auto* rwbuf = shu::get_user_data<stream_with_buf>(*s);
        // // shu::copy_from_buffers(rwbuf->wt_buf.value(), buf);
        // rwbuf->wt_buf.value().assign(buf[0].p, buf[0].p + buf[0].size);
        buffer_t wbuf;
        wbuf.p = response.data();
        wbuf.size = response.size();
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
    std::unordered_map<int, sstream*> cons_;
    bool servererr_;
};

int main() {
    using namespace shu;

    sloop loop;
    tcp_server server(loop, {8888});
    loop.run();
    return 0;
}