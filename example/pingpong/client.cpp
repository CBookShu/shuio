#include <iostream>
#include <atomic>
#include "shuio/shuio.h"

using namespace shu;

class Client;
class Session {
    sloop* loop_;
    sstream* s_;
    Client* owner_;
    int64_t bytesRead_;
    int64_t bytesWritten_;
    int64_t messagesRead_;
    addr_storage_t addr_;
    sclient client_;
    std::vector<char> rd_buffer_;
    std::vector<char> wt_buffer_;

    enum {
        suggest_buf_alloc = 4096
    };
public:
    Session(sloop* loop, Client* owner, shu::addr_storage_t& addr)
    :loop_(loop), s_(nullptr), owner_(owner),
    bytesRead_(0),bytesWritten_(0),messagesRead_(0),
    addr_(addr)
    {

    }

    void start() {
        client_.start(loop_, addr_, {
            .evClose = [this](sclient* ){
                on_close();
            },
            .evConn = [this](socket_io_result res, ssocket* s, const addr_pair_t& addr_pair){
                on_connect(res, s, addr_pair);
            },
        });
    }

    void stop() {
        if (s_) {
            s_->stop();
        }
    }

    void on_connect(socket_io_result res, ssocket* s, const addr_pair_t& addr_pair);
    void on_close();

    void on_read(socket_io_result_t res, buffers_t bufs) {
        if(res.res <= 0) {
            s_->stop();
            return;
        }

        ++messagesRead_;
        bytesRead_ += res.res;
        bytesWritten_ += res.res;

        wt_buffer_.assign(bufs[0].p, bufs[0].p + bufs[0].size);
        wt_buffer_.insert(
            wt_buffer_.end(),
            bufs[1].p, bufs[1].p + bufs[1].size
        );

        buffer_t buf;
        buf.p = wt_buffer_.data();
        buf.size = wt_buffer_.size();
        s_->write(buf, [this](sstream* s, socket_io_result res){
            on_write(res);
        });
    }

    void on_write(socket_io_result_t res) {

    }

    int64_t bytesRead() const
    {
        return bytesRead_;
    }

    int64_t messagesRead() const
    {
        return messagesRead_;
    }
};

class Client {
    sloop* loop_;
    int blockSize_;
    int sessionCount_;

    std::string message_;
    std::atomic_int numConnected_;
    int timeout_;

    std::vector<std::unique_ptr<Session>> sessions_;
public:
    Client(sloop* loop,
        int blockSize,
        int sessionCount,
        int timeout,
        int threadCount,
        shu::addr_storage_t addr)
    :loop_(loop),blockSize_(blockSize),sessionCount_(sessionCount),numConnected_{0},timeout_(timeout)
    {

        loop_->add_timer([this](){
            handleTimeout();
        }, std::chrono::seconds(timeout));

        for (int i = 0; i < blockSize; ++i)
        {
            message_.push_back(static_cast<char>(i % 128));
        }

        for (int i = 0; i < sessionCount; ++i) {
            auto* session = new Session(loop, this, addr);
            sessions_.emplace_back(session);
            session->start();
        }
    }

    void onConnect() {
        if (numConnected_.fetch_add(1, std::memory_order::relaxed) == (sessionCount_ - 1))
        {
            std::cout << "all connected" << std::endl;
        }
    }

    void onDisconnect() {
        if (numConnected_.fetch_sub(1, std::memory_order::relaxed) == 1) {
            std::cout << "all disconnected" << std::endl;

            int64_t totalBytesRead = 0;
            int64_t totalMessagesRead = 0;
            for (const auto& session : sessions_)
            {
                totalBytesRead += session->bytesRead();
                totalMessagesRead += session->messagesRead();
            }
            std::cout << totalBytesRead << " total bytes read" << std::endl;
            std::cout << totalMessagesRead << " total messages read" << std::endl;
            std::cout << static_cast<double>(totalBytesRead) / static_cast<double>(totalMessagesRead)
                    << " average message size" << std::endl;
            std::cout << static_cast<double>(totalBytesRead) / (timeout_ * 1024 * 1024)
                    << " MiB/s throughput" << std::endl;
            loop_->stop();
        }
    }

    void handleTimeout()
    {
        std::cout << "stop" << std::endl;;
        for (auto& session : sessions_)
        {
            session->stop();
        }
    }

    std::string& getMessage() {
        return message_;
    }
};

int main() {
    sloop loop;
    addr_storage_t addr(8888, "127.0.0.1");
    Client client(&loop, 512, 128, 10, 1, addr);
    loop.run();
    return 0;
}

void Session::on_connect(socket_io_result res, ssocket* s, const addr_pair_t& addr_pair)  {
    auto sock_ptr = UPtr<ssocket>(s);
    if (res.res <= 0) {
        client_.stop();
        return;
    }
    owner_->onConnect();

    auto stream_ptr = std::make_unique<sstream>();
    stream_ptr->start(loop_, sock_ptr.release(), {}, {
        .evClose = [this](sstream* s) {
            on_close();
        }
    });
    s_ = stream_ptr.release();
    s_->read([this](sstream*s, socket_io_result res, buffers_t bufs){
        on_read(res, bufs);
    },
    [this](sstream*s, buffer_t& buf){
        rd_buffer_.resize(suggest_buf_alloc);
        buf.p = rd_buffer_.data();
        buf.size = rd_buffer_.size();
    });

    buffer_t buf;
    buf.p = owner_->getMessage().data();
    buf.size = owner_->getMessage().size();
    s_->write(buf, [this](sstream*s, socket_io_result res){
        on_write(res);
    });
}

void Session::on_close() {
    delete s_;
    s_ = nullptr;
    owner_->onDisconnect();
}