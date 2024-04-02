#include "shuio/shu_stream.h"
#include "linux_detail.h"
#include "shuio/shu_buffer.h"
#include "shuio/shu_loop.h"
#include <shuio/shu_socket.h>

#include <vector>

namespace shu {
    struct sstream::sstream_t : public std::enable_shared_from_this<sstream_t> {
        sstream_opt opt_;
        sloop* loop_;
        std::unique_ptr<ssocket> sock_;

        std::unique_ptr<socket_buffer> read_buffer_;
        std::vector<socket_buffer> write_buffer_;

        io_uring_task_union read_complete_;
        sstream::func_read_t read_cb_;
        std::shared_ptr<sstream_t> read_holder_;

        io_uring_task_union write_complete_;
        sstream::func_write_t write_cb_;
        std::shared_ptr<sstream_t> write_holder_;

        bool stop_;
        
        sstream_t(sloop* loop, ssocket* sock, sstream_opt opt, sstream::func_read_t&& read_cb, sstream::func_write_t&& write_cb) {
            loop_ = loop;
            sock_.reset(sock);
            opt_ = opt;

            read_buffer_ = std::make_unique<socket_buffer>(opt.read_buffer_init);

            read_cb_ = std::forward<sstream::func_read_t>(read_cb);
            write_cb_ = std::forward<sstream::func_write_t>(write_cb);
            stop_ = false;
        }

        ~sstream_t() {
            if (sock_ && sock_->valid()) {
                sock_->shutdown();
                sock_->close();
            }
        }

        void start() {
            read_complete_ = io_uring_socket_t();
            std::get_if<io_uring_socket_t>(&read_complete_)->cb = [this](io_uring_cqe* cqe){
                run_read(cqe);
            };
            write_complete_ = io_uring_socket_t{};
            std::get_if<io_uring_socket_t>(&write_complete_)->cb = [this](io_uring_cqe* cqe){
                run_write(cqe);
            };

            post_read();
        }

        void post_write(socket_buffer* buf) {
            if(stop_) {
                return;
            }
			if (buf && !buf->ready().empty()) {
				write_buffer_.emplace_back(std::move(*buf));
			}

            if(write_holder_) {
                return;
            }

            io_uring_push_sqe(loop_, [this](io_uring* ring){
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                std::vector<struct iovec> bufs(write_buffer_.size());
                for(int i = 0; i < bufs.size(); ++i) {
                    auto b = write_buffer_[i].ready();
                    bufs[i].iov_base = b.data();
                    bufs[i].iov_len = b.size();
                }
                auto* sock = navite_cast_ssocket(sock_.get());
                io_uring_prep_writev(sqe, sock->fd, bufs.data(), bufs.size(), 0);
                io_uring_sqe_set_data(sqe, &write_complete_);
                io_uring_submit(ring);
            });

            write_holder_ = shared_from_this();
        }

        void post_read() {
            if(stop_) {
                return;
            }
            assert(!read_holder_);
            io_uring_push_sqe(loop_, [this](io_uring* ring){
                struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
                auto buf = read_buffer_->prepare(opt_.read_buffer_count_per_op);

                auto* sock = navite_cast_ssocket(sock_.get());
                io_uring_prep_recv(sqe, sock->fd, buf.data(), buf.size(), 0);
                io_uring_sqe_set_data(sqe, &read_complete_);
                io_uring_submit(ring);
            });

            read_holder_ = shared_from_this();
        }

        void run_read(io_uring_cqe* cqe) {
            auto tmp = std::move(read_holder_);

            read_ctx_t ctx{ .buf = *read_buffer_ };
            socket_io_result res;
            if(cqe->res < 0) {
                res = {.bytes = 0, .err = -1, .naviteerr = cqe->res};
            } else if(cqe->res > 0) {
                res = {.bytes = cqe->res, .err = 0, .naviteerr = 0};
                read_buffer_->commit(cqe->res);
            } else if(cqe->res == 0) {
                res = {.bytes = cqe->res, .err = -1, .naviteerr = 0};
            }
            read_cb_(res, ctx);

            if(res.err != 0) {
                return;
            }

            post_read();
        }

        void run_write(io_uring_cqe* cqe) {
            auto tmp = std::move(write_holder_);
            socket_io_result_t res;
            write_ctx_t ctx{ .bufs = write_buffer_ };
            if(cqe->res < 0) {
                res = socket_io_result_t{ .bytes = 0,.err = -1, .naviteerr = cqe->res };
            } else if(cqe->res > 0) {
                res = socket_io_result_t{ .bytes = cqe->res, .err = 0, .naviteerr = 0 };
            } else if(cqe->res == 0) {
                res = socket_io_result_t{ .bytes = 0, .err = 0, .naviteerr = 0 };
            }
            
            write_cb_(res, ctx);

            auto total = cqe->res;
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
				post_write(nullptr);
			}
        }

        void stop() {
            if(std::exchange(stop_, true)) {
                return;
            }
            sock_->close();
            // io_uring_push_sqe(loop_, [&](io_uring* ring){
            //     auto* navie_sock = navite_cast_ssocket(sock_.get());
            //     struct io_uring_sqe *read_sqe = io_uring_get_sqe(ring);
            //     io_uring_prep_cancel(read_sqe, &read_complete_, 0);

            //     struct io_uring_sqe *write_sqe = io_uring_get_sqe(ring);
            //     io_uring_prep_cancel(write_sqe, &write_complete_, 0);
            //     io_uring_submit(ring);
            // });
        }
    };

    sstream::sstream() {
    }
    sstream::sstream(sstream&& other) noexcept {
        s_.swap(other.s_);
    }
    sstream::~sstream() {
        if(auto sptr = s_.lock()) {

        }
    }

    // just call once after new
    auto sstream::start(sloop* l, ssocket* s, sstream_opt opt,
		sstream::func_read_t&& rcb, sstream::func_write_t&& wcb) -> void {

        auto sptr = std::make_shared<sstream::sstream_t>(l,s,opt,
            std::forward<func_read_t>(rcb),std::forward<func_write_t>(wcb));
        s_ = sptr;
        l->dispatch([sptr](){
            sptr->start();
        });
    }
    // call after start read
    void sstream::write(socket_buffer&& buff){
        if(auto sptr = s_.lock()) {
            sptr->loop_->dispatch([sptr, buf = {std::move(buff)}] () mutable{
                sptr->post_write(const_cast<socket_buffer*>(buf.begin()));
            });
        }
    }

    void sstream::stop() {
        if(auto sptr = s_.lock()) {
            sptr->loop_->dispatch([sptr](){
                sptr->stop();
            });
        }
    }
};