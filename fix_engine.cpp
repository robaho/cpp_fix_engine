#include "fix_engine.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "fix.h"
#include "msg_logon.h"
#include "msg_logout.h"
#include "socketbuf.h"

template <class SessionConfig>
void Acceptor<SessionConfig>::listen() {
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return;
    }
    struct sockaddr_in serverAddress;

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt SO_REUSEADDR failed");
        return;
    }
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt SO_REUSEPORT failed");
        return;
    }

    if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Error binding socket." << std::endl;
        return;
    }

    typedef boost::fibers::buffered_channel<Session<SessionConfig> *> channel_t;
    channel_t chan{2};

    workerThreads = std::max(workerThreads,1);

    std::cout << "starting " << workerThreads << " worker threads\n";
    std::vector<std::thread> workers;

    boost::fibers::barrier b(workerThreads+1);

    for (int i = 0; i < workerThreads; i++) {
        workers.push_back(std::thread(
            [&chan, &b] {
                boost::fibers::use_scheduling_algorithm<boost::fibers::algo::shared_work>(true);
                // wait till all threads joined the shared pool
                b.wait();
                while (true) {
                    Session<SessionConfig> *session;
                    if (chan.pop(session) != boost::fibers::channel_op_status::success) {
                        return;
                    }
                    auto fiber = boost::fibers::fiber(&Session<SessionConfig>::handle, session);
                    fiber.detach();
                }
            }));
    }
    // wait till all threads joined the shared pool
    b.wait();

    std::cout << "starting poller\n";
    std::thread poller_thread([this]() {
        while (true) {
            try {
                poller.poll();
            } catch (std::runtime_error &err) {
                std::cerr << "poller error: " << err.what() << "\n";
                return;
            }
        }
    });

    std::cout << "listening for connections on port " << port << "\n";
    if (::listen(serverSocket, 5) < 0) {  // 5 is the backlog size
        std::cerr << "error listening for connections" << std::endl;
        return;
    }

    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        char ip_str[INET_ADDRSTRLEN];

        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (clientSocket < 0) {
            if(errno==EBADF) break;
            perror("error accepting connection");
            continue;
        }

        int flag = 1;
        if (setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
            perror("unable to set TCP_NODELAY");
        }

        int flags = fcntl(clientSocket, F_GETFL, 0);
        if (fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK) < 0) {
            perror("unable to set O_NONBLOCK");
            close(clientSocket);
            continue;
        }

        inet_ntop(AF_INET, &(clientAddr.sin_addr), ip_str, INET_ADDRSTRLEN);
        std::cout << "connection from " << ip_str << " port " << ntohs(clientAddr.sin_port) << " on thread " << std::this_thread::get_id() << "\n";
        try {
            auto session = new Session(clientSocket, *this, config);
            chan.push(session);

            onConnected(clientAddr);
            poller.add_socket(clientSocket, session, [](struct kevent &event, void *data) {
                auto session = static_cast<Session<SessionConfig> *>(data);
                if (event.flags & EV_EOF) {
                    close(session->socket);
                }
                session->unpark();
            });
        } catch (std::runtime_error &err) {
            std::cerr << "acceptor refused connection: " << err.what() << "\n";
        }
    }
    // close the sockets for any active sessions
    {
        std::unique_lock<std::shared_mutex> lu(sessionLock);
        for(auto entry : sessionMap) {
            close(entry.second->socket);
            entry.second->unpark();
        }
    }

    // wait for sessions to terminate
    while(true) {
        {
            std::unique_lock<std::shared_mutex> lu(sessionLock);
            if(sessionMap.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    poller.close();
    poller_thread.join();

    chan.close();

    for (auto &worker : workers) worker.join();
}

template <class SessionConfig>
void Session<SessionConfig>::handle() {
    DisconnectHandler disconnectHandler(*this, handler);
    Socketbuf sbuf(socket, *this);
    std::istream is(&sbuf);
    FixMessage msg;
    FixBuilder out;
    try {
        // std::cout << "handling session " << config << " on thread " << std::this_thread::get_id()<<"\n";
        while (true) {
            FixMessage::parse(is, msg, GroupDefs());
            if (is.eof()) {
                return;
            }

            if (!loggedIn && msg.msgType() != Logon::msgType) {
                std::cerr << "rejecting connection, " << msg.msgType() << " is not a Logon\n";
                Logout::build(out, "not logged in");
                sendMessage(Logout::msgType, out);
                return;
            }
            if (msg.seqNum() != config.expectedSeqNum) {
                std::cerr << "rejecting connection, " << msg.seqNum() << " != expected " << config.expectedSeqNum << "\n";
                Logout::build(out, "invalid sequence number");
                sendMessage(Logout::msgType, out);
                return;
            }
            auto targetCompId = msg.getString(Tag::TARGET_COMP_ID);
            if (targetCompId != config.senderCompId) {
                std::cerr << "rejecting connection, invalid target comp id " << targetCompId << ", expected " << config.senderCompId << "\n";
                Logout::build(out, "invalid target comp id");
                sendMessage(Logout::msgType, out);
                return;
            }

            auto senderCompId = msg.getString(Tag::SENDER_COMP_ID);
            if (senderCompId != config.targetCompId) {
                if (config.targetCompId == "*") {
                    config.targetCompId = senderCompId;
                } else {
                    std::cerr << "rejecting connection, invalid sender comp id " << senderCompId << ", expected " << config.targetCompId << "\n";
                    Logout::build(out, "invalid sender comp id");
                    sendMessage(Logout::msgType, out);
                    return;
                }
            }

            if (!loggedIn) {
                if (!handler.validateLogon((msg))) {
                    std::cerr << "logon rejected\n";
                    Logout::build(out, "invalid logon");
                    sendMessage(Logout::msgType, out);
                    return;
                }
                config.initialize(msg);
                Logon::build(out);
                sendMessage(Logon::msgType, out);
                loggedIn = true;
                handler.onLoggedOn(*this);
            }
            handler.onMessage(*this, msg);
            config.expectedSeqNum++;
        }
    } catch (const std::runtime_error &err) {
        std::cerr << "exception processing session: " << config << ", "
                  << err.what() << "\n";
        return;
    }
}

template <class SessionConfig>
void Initiator<SessionConfig>::connect() {
    if ((socket = ::socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return;
    }
    std::cout << "connecting...\n";
    if (::connect(socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("socket connect");
        return;
    }

    int flag = 1;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        perror("unable to set TCP_NODELAY");
    }

    session = new Session(socket, *this, config);

    if (poller) {
        int flags = fcntl(socket, F_GETFL, 0);
        if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
            perror("unable to set O_NONBLOCK");
            close(socket);
            return;
        }
        poller->add_socket(socket, session, [](struct kevent &event, void *data) {
            auto session = static_cast<Session<SessionConfig> *>(data);
            if (event.flags & EV_EOF) {
                close(session->socket);
            }
            session->unpark();
        });
    }
    connected = true;
    onConnected();
}

template void Acceptor<DefaultSessionConfig>::listen();
template void Initiator<DefaultSessionConfig>::connect();
