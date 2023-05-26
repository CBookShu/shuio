#include "shuio/shu_stream.h"
#include "linux_detail.h"
#include "shuio/shu_buffer.h"
#include "shuio/shu_loop.h"

#include <vector>

namespace shu {

    struct uring_read_op;
    struct uring_write_op;
    struct sstream::sstream_t {
        sstream_opt opt;
        sloop* loop;
        ssocket* sock;
        uring_read_op* read_op;
        uring_write_op* write_op;
        sstream_runable* cb;
    };

    struct uring_stream_compelte : uring_ud_io_t {
        std::shared_ptr<sstream> hold;
    };

    struct uring_read_op : uring_callback {
        socket_buffer* buffer{nullptr};
        std::shared_ptr<sstream> s;
        uring_stream_compelte complete;
        uring_stream_compelte cancel;

        ~uring_read_op() {
            if(buffer) {
                delete buffer;
            }
        }
        void init() {
            complete.type = op_type::type_io;
            complete.cb = this;
            cancel.type = op_type::type_io;
            cancel.cb = this;
            buffer = new socket_buffer{s->option()->read_buffer_init};
            post_read();
        }
        void post_read() {
            if(complete.hold) return;
            io_uring_push_sqe(s->loop(), [this](io_uring* ring){
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                auto buf = buffer->prepare(s->option()->read_buffer_count_per_op);

                auto* sock = navite_cast_ssocket(s->sock());
                io_uring_prep_recv(sqe, sock->fd, buf.data(), buf.size(), 0);
                complete.hold = s;
                io_uring_sqe_set_data(sqe, &complete);
                io_uring_submit(ring);
            });
        }
        void post_cancel() {
            if(cancel.hold) {
                return;
            }
            if(!complete.hold) {
                return;
            }
            io_uring_push_sqe(s->loop(), [this](io_uring* ring){
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                io_uring_prep_cancel(sqe, &complete, 0);
                cancel.hold = s;
                io_uring_sqe_set_data(sqe, &cancel);
                io_uring_submit(ring);
            });
        }
        virtual void run(io_uring_cqe* cqe) noexcept override {
            uring_stream_compelte* c = (uring_stream_compelte*)cqe->user_data;
            if(cqe->res < 0) {
                socket_io_result_t res{ .bytes = 0,.err = -1, .naviteerr = cqe->res };
                s->handle()->cb->on_read(res, s.get());
            } else {
                socket_io_result_t res{ .bytes = cqe->res };
                buffer->commit(cqe->res);
                s->handle()->cb->on_read(res, s.get());
            }
            if(c == &complete && cancel.hold) {
                c->hold.reset();
                return;
            }
            if(c == &cancel) {
                c->hold.reset();
                s.reset();
                return;
            }
            c->hold.reset();
            post_read();
        }
    };
    struct uring_write_op : uring_callback {
        std::vector<socket_buffer*> buffers{};
        std::shared_ptr<sstream> s;
        uring_stream_compelte complete;
        uring_stream_compelte cancel;

        ~uring_write_op() {
            for(auto& b:buffers) {
                delete b;
            }
        }
        void init() {
            complete.type = op_type::type_io;
            complete.cb = this;
            cancel.type = op_type::type_io;
            cancel.cb = this;
        }
        void post_write(socket_buffer* buff) {
            if (buff) {
                buffers.push_back(buff);
            }
            if(!s) {
                return;
            }
            if(complete.hold) {
                return;
            }
            
            io_uring_push_sqe(s->loop(), [this](io_uring* ring){
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                std::vector<struct iovec> bufs(buffers.size());
                for(int i = 0; i < bufs.size(); ++i) {
                    auto b = buffers[i]->ready();
                    bufs[i].iov_base = b.data();
                    bufs[i].iov_len = b.size();
                }
                auto* sock = navite_cast_ssocket(s->sock());
                io_uring_prep_writev(sqe, sock->fd, bufs.data(), bufs.size(), 0);
                complete.hold = s;
                io_uring_sqe_set_data(sqe, &complete);
                io_uring_submit(ring);
            });
        }
        void post_cancel() {
            if(!complete.hold) {
                return;
            }
            if(cancel.hold) {
                return;
            }
            io_uring_push_sqe(s->loop(), [this](io_uring* ring){
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                io_uring_prep_cancel(sqe, &complete, 0);
                cancel.hold = s;
                io_uring_sqe_set_data(sqe, &cancel);
                io_uring_submit(ring);
            });
        }
        virtual void run(io_uring_cqe* cqe) noexcept override {
            uring_stream_compelte* c = (uring_stream_compelte*)cqe->user_data;
            if(cqe->res < 0) {
                socket_io_result_t res{ .bytes = 0,.err = -1, .naviteerr = cqe->res };
                s->handle()->cb->on_write(res, s.get());
            } else {
                socket_io_result_t res{ .bytes = cqe->res };
                s->handle()->cb->on_write(res, s.get());
            }
            
            if(cancel.hold && c == &complete) {
                c->hold.reset();
                return;
            }
            if(c == &cancel) {
                c->hold.reset();
                s.reset();
                return;
            }
            c->hold.reset();

            auto total = cqe->res;
            auto eraseit = buffers.begin();
            auto do_erase = false;
            for (auto it = buffers.begin(); it != buffers.end(); ++it) {
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
                buffers.erase(buffers.begin(), eraseit + 1);
            }
            if (!buffers.empty()) {
                // 继续完成未完成的写入事业！
                post_write(nullptr);
            }
        }
    };

    sstream::sstream(sloop* loop,ssocket* sock,sstream_opt opt) {
        _s = new sstream_t{};
        _s->opt = opt;
        _s->loop = loop;
        _s->sock = sock;
    }
    sstream::sstream(sstream&& other) noexcept {
        _s = std::exchange(other._s, nullptr);
    }
    sstream::~sstream() {
        if(!_s) return;

        if(_s->sock) {
            delete _s->sock;
        }

        delete _s->write_op;
        delete _s->read_op;

        delete _s;
    }

    auto sstream::handle() -> struct sstream_t* {
        return _s;
    }
    auto sstream::option() -> const sstream_opt* {
        return &_s->opt;
    }
    auto sstream::option() const -> const sstream_opt* {
        return &_s->opt;
    }
    auto sstream::loop() -> sloop* {
        return _s->loop;
    }
    auto sstream::sock() -> ssocket* {
        return _s->sock;
    }
    auto sstream::readbuffer() -> socket_buffer* {
        return _s->read_op->buffer;
    }
    auto sstream::writebuffer() -> std::span< socket_buffer*> {
        return _s->write_op->buffers;
    }
    // just call once after new
    auto sstream::start_read(sstream_runable* r) -> void {
        _s->cb = r;
        _s->read_op = new uring_read_op{};
        _s->read_op->s = shared_from_this();
        _s->write_op = new uring_write_op{};
        _s->write_op->s = shared_from_this();
        _s->read_op->init();
        _s->write_op->init();
    }
    // call after start read
    auto sstream::write(socket_buffer* buff) -> bool {
        auto self = shared_from_this();
        _s->loop->post_f([self, this, buff](){
            _s->write_op->post_write(buff);
        });
    }
    // call after start read
    auto sstream::close() -> void {
        auto self = shared_from_this();
        _s->loop->post_f([self, this](){
            _s->write_op->post_cancel();
            _s->read_op->post_cancel();
        });
    }
};