#include "shuio/shu_stream.h"
#include "linux_detail.h"
#include "shuio/shu_buffer.h"
#include "shuio/shu_loop.h"
#include <shuio/shu_socket.h>

#include <vector>
#include <bitset>

namespace shu {
    struct sstream::sstream_t : public std::enable_shared_from_this<sstream_t> {
        sstream_opt opt_;
        sloop* loop_;
        std::unique_ptr<ssocket> sock_;

        std::unique_ptr<socket_buffer> read_buffer_;
        std::vector<socket_buffer> write_buffer_;
        std::shared_ptr<sstream_t> holder_;

        io_uring_task_union read_complete_;
        sstream::func_read_t read_cb_;

        io_uring_task_union write_complete_;
        sstream::func_write_t write_cb_;

        bool stop_;
        bool reading_;
        bool writing_;
        
        sstream_t(sloop* loop, ssocket* sock, sstream_opt opt, sstream::func_read_t&& read_cb, sstream::func_write_t&& write_cb) {
            loop_ = loop;
            sock_.reset(sock);
            opt_ = opt;

            read_buffer_ = std::make_unique<socket_buffer>(opt.read_buffer_init);

            read_cb_ = std::forward<sstream::func_read_t>(read_cb);
            write_cb_ = std::forward<sstream::func_write_t>(write_cb);

            stop_ = false;
            reading_ = false;
            writing_ = false;
        }

        ~sstream_t() {

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
            holder_ = shared_from_this();
            post_read();
        }

        void check_close_and_destroy() {
            if (!reading_ && !writing_) {
                holder_.reset();
            }
        }

        void post_write(socket_buffer* buf) {
            if (buf) {
                if (stop_) {
                    // 准备停止了，别写了，省的一直有buff 停不下来
                    // panic?
                    return;
                }
                if (!buf->ready().empty()) {
                    write_buffer_.emplace_back(std::move(*buf));
                }
            }

            if (std::exchange(writing_, true)) {
                return;
            }
            
            io_uring_push_sqe(loop_, [this](io_uring* ring){
                struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
                std::vector<struct iovec> bufs(write_buffer_.size());
                for(std::size_t i = 0; i < bufs.size(); ++i) {
                    auto b = write_buffer_[i].ready();
                    bufs[i].iov_base = b.data();
                    bufs[i].iov_len = b.size();
                }
                auto* sock = navite_cast_ssocket(sock_.get());
                io_uring_prep_writev(sqe, sock->fd, bufs.data(), bufs.size(), 0);
                io_uring_sqe_set_data(sqe, &write_complete_);
                io_uring_submit(ring);
            });
        }

        void post_read() {
            reading_ = true;
            io_uring_push_sqe(loop_, [this](io_uring* ring){
                struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
                auto buf = read_buffer_->prepare(opt_.read_buffer_count_per_op);

                auto* sock = navite_cast_ssocket(sock_.get());
                io_uring_prep_recv(sqe, sock->fd, buf.data(), buf.size(), 0);
                io_uring_sqe_set_data(sqe, &read_complete_);
                io_uring_submit(ring);
            });
        }

        void run_read(io_uring_cqe* cqe) {
            reading_ = false;
            auto _finaly = s_defer([this, self = holder_](){
                check_close_and_destroy();
            });
            read_ctx_t ctx{ .buf = *read_buffer_ };
            socket_io_result res;
            if(cqe->res < 0) {
                res = {.bytes = 0, .err = -1, .naviteerr = cqe->res};
            } else if(cqe->res > 0) {
                res = {.bytes = static_cast<std::uint32_t>(cqe->res), .err = 0, .naviteerr = 0};
                read_buffer_->commit(cqe->res);
            } else if(cqe->res == 0) {
                res = {.bytes = 0, .err = -1, .naviteerr = 0};
            }
            read_cb_(res, ctx);

            if(res.err != 0) {
                // 读失败 主动停止
                if (writing_) {
                    stop();
                }
                return;
            }

            post_read();
        }

        void run_write(io_uring_cqe* cqe) {
            writing_ = false;
            auto _finaly = s_defer([this, self = holder_](){
                check_close_and_destroy();
            });

            socket_io_result_t res;
            write_ctx_t ctx{ .bufs = write_buffer_ };
            if(cqe->res < 0) {
                res = socket_io_result_t{ .bytes = 0,.err = -1, .naviteerr = cqe->res };
            } else if(cqe->res > 0) {
                res = socket_io_result_t{ .bytes = static_cast<std::uint32_t>(cqe->res), .err = 0, .naviteerr = 0 };
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

            if(res.err < 0) {
                // 写入失败了 这里都写入失败了，还需要shutdown？
                if (sock_ && sock_->valid()) {
                    sock_->shutdown(shutdown_type::shutdown_write);
                }
                return;
            }

			if (!write_buffer_.empty()) {
				post_write(nullptr);
			}

            if (!writing_ && stop_) {
                // 要关闭了
                sock_->shutdown(shutdown_type::shutdown_write);
            }
        }

        void stop() {
            if (std::exchange(stop_, true)) {
                return;
            }

            // 等所有正在write的Buffer 发过去，以防整个包发一半
            if (writing_) {
                return;
            }
            auto _finally = s_defer([this, self = holder_](){
                check_close_and_destroy();
            });
            // 直接关闭写，通知对方
            sock_->shutdown(shutdown_type::shutdown_write);
            writing_ = false;
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