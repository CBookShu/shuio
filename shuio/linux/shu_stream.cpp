#include "shuio/shu_stream.h"
#include "linux_detail.h"
#include "shuio/shu_buffer.h"
#include "shuio/shu_loop.h"
#include <shuio/shu_socket.h>

#include <vector>
#include <bitset>

namespace shu {
    struct sstream::sstream_t {
        sstream_opt opt_;
        sloop* loop_;
        sstream* owner_;
        std::unique_ptr<ssocket> sock_;
        stream_ctx_t cb_ctx_;
        
        io_uring_task_union read_complete_;
        func_on_read_t on_read_cb_;
        func_alloc_t on_alloc_cb_;

        io_uring_task_union write_complete_;
        func_on_write_t on_write_cb_;

        bool stop_;
        bool reading_;
        bool writing_;
        bool close_;
        
        std::any ud_;

        sstream_t(sloop* loop, sstream* owner, ssocket* sock, sstream_opt opt, stream_ctx_t&& cb_ctx) 
        : loop_(loop), owner_(owner),sock_(sock), opt_(opt), cb_ctx_(std::forward<stream_ctx_t>(cb_ctx)),
        stop_(false), reading_(false), writing_(false), close_(false)
        {
            
        }

        void post_to_close() {
            if (std::exchange(close_, true)) {
                return;
            }
            sock_->shutdown();
            if (cb_ctx_.evClose) {
                loop_->post_inloop([f = std::move(cb_ctx_.evClose), owner=owner_](){
                    f(owner);
                });
            }
        }

        int start() {
            read_complete_ = io_uring_socket_t();
            std::get_if<io_uring_socket_t>(&read_complete_)->cb = [this](io_uring_cqe* cqe){
                run_read(cqe);
            };
            write_complete_ = io_uring_socket_t{};
            std::get_if<io_uring_socket_t>(&write_complete_)->cb = [this](io_uring_cqe* cqe){
                run_write(cqe);
            };
            return 1;
        }

        int init_read(func_on_read_t&& cb, func_alloc_t&& alloc) {
			shu::panic(!reading_);
			if (auto err = sock_->noblock(true); err <= 0) {
				socket_io_result res{err};	
				std::forward<func_on_read_t>(cb)(owner_, res, buffers_t{});
				return err;
			}

			if (auto err = sock_->nodelay(true); err <= 0) {
				socket_io_result res{err};
                std::forward<func_on_read_t>(cb)(owner_, res, buffers_t{});
				return err;
			}

            on_read_cb_ = std::forward<func_on_read_t>(cb);
            on_alloc_cb_ = std::forward<func_alloc_t>(alloc);
			return post_read();
		}

        bool post_write(buffers_t bufs, func_on_write_t&& cb) {
            if(writing_) {
                return false;
            }

            auto* sock = navite_cast_ssocket(sock_.get());
            io_uring_push_sqe(loop_, [&](io_uring* ring){
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                
                io_uring_prep_writev(sqe, sock->fd, reinterpret_cast<iovec*>(bufs.data()), bufs.size(), 0);
                io_uring_sqe_set_data(sqe, &write_complete_);
                io_uring_submit(ring);
            });

            on_write_cb_ = std::forward<func_on_write_t>(cb);
            writing_ = true;
            return true;
        }

        int post_read() {
            reading_ = true;
            io_uring_push_sqe(loop_, [this](io_uring* ring){
                struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
                auto* sock = navite_cast_ssocket(sock_.get());
                io_uring_prep_recv(sqe, sock->fd, nullptr, 0, 0);
                io_uring_sqe_set_data(sqe, &read_complete_);
                io_uring_submit(ring);
            });
            return 1;
        }

        void run_read(io_uring_cqe* cqe) {
            reading_ = false;

            socket_io_result res{.res = cqe->res};
            if(cqe->res < 0) {
                on_read_cb_(owner_, res, buffers_t{});
            } else {
                char buffer_stack[65535];
                buffer_t buf[2];
                shu::zero_mem(buf[0]);
                if(on_alloc_cb_) {
                    on_alloc_cb_(owner_, buf[0]);
                }
                
                buf[1].p = buffer_stack;
                buf[1].size = sizeof(buffer_stack);

                auto* navite_sock = shu::navite_cast_ssocket(sock_.get());
                auto r = ::readv(navite_sock->fd, (iovec*)buf, 2);
                if (r <= 0) {
                    res.res = -s_last_error();
                    on_read_cb_(owner_, res, buffers_t{});
                } else {
                    res.res = r;
                    auto diff = res.res - static_cast<int>(buf[0].size);
                    if (diff >= 0) {
                        buf[1].size = diff;
                    } else {
                        buf[1].size = 0;
                        buf[0].size = res.res;
                    }
                    on_read_cb_(owner_, res, buf);
                }
            }
            
            if (stop_) {
                if (!writing_) {
                    post_to_close();
                }
            } else {
                post_read();
            }

        }

        void run_write(io_uring_cqe* cqe) {
            writing_ = false;

            socket_io_result res{.res = cqe->res};
            
            on_write_cb_(owner_, res);

            if (stop_ && !reading_) {
                post_to_close();
            }
        }

        void stop() {
            if (std::exchange(stop_, true)) {
                return;
            }

            if (writing_) {
                return;
            }

            if (reading_) {
                io_uring_push_sqe(loop_, [&](io_uring* ring){
                    auto* navie_sock = navite_cast_ssocket(sock_.get());
                    struct io_uring_sqe *read_sqe = io_uring_get_sqe(ring);
                    io_uring_prep_cancel(read_sqe, &read_complete_, 0);
                    io_uring_submit(ring);
                });
            } else {
                post_to_close();
            }
        }
    };

    sstream::sstream():s_(nullptr) {
    }
    sstream::sstream(sstream&& other) noexcept {
        s_ = std::exchange(other.s_, nullptr);
    }
    sstream::~sstream() {
        if(s_) {
            delete s_;
        }
    }

    int sstream::start(sloop* loop, ssocket* sock, sstream_opt opt, 
			stream_ctx_t&& stream_event){
        shu::panic(!s_);
        s_ = new sstream_t(loop, this, sock, opt, std::forward<stream_ctx_t>(stream_event));
        return s_->start();
    }

    auto sstream::get_ud() -> std::any* {
        shu::panic(s_);
        return &s_->ud_;        
    }

    int sstream::read(func_on_read_t&& cb, func_alloc_t&& alloc) {
        shu::panic(s_);
        return s_->init_read(std::forward<func_on_read_t>(cb), std::forward<func_alloc_t>(alloc));
    }

    // call after start read
    bool sstream::write(buffer_t buf, func_on_write_t&& cb){
        shu::panic(s_);
        buffer_t bufs[1] = {buf};
        return s_->post_write(bufs, std::forward<func_on_write_t>(cb));
    }

    bool sstream::write(buffers_t bufs, func_on_write_t&& cb) {
        shu::panic(s_);
        return s_->post_write(bufs, std::forward<func_on_write_t>(cb));
    }

    void sstream::stop() {
        shu::panic(s_);
        s_->stop();
    }
};