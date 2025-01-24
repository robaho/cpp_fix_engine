#include "fix_engine.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "fix.h"
#include "msg_logon.h"
#include "msg_logout.h"
#include "socketbuf.h"

void Acceptor::listen() {
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

    std::cout << "listening for connections... on port " << port << "\n";

    while (true) {
        if (::listen(serverSocket, 5) < 0) {  // 5 is the backlog size
            std::cerr << "error listening for connections" << std::endl;
            return;
        }
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        char ip_str[INET_ADDRSTRLEN];

        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (clientSocket < 0) {
            std::cerr << "error accepting connection" << std::endl;
        } else {
            inet_ntop(AF_INET, &(clientAddr.sin_addr), ip_str, INET_ADDRSTRLEN);
            std::cout << "connection from " << ip_str << " port " << ntohs(clientAddr.sin_port) << "\n";
            try {
                onConnected(clientAddr);
                auto session = new Session(clientSocket, *this, config);
                auto thread = new std::thread(&Session::handle, session);
                session->thread = thread;
                threads.push_back(thread);
            } catch (std::runtime_error &err) {
                std::cerr << "Acceptor refused connection: " << err.what() << "\n";
            }
        }
    }
    // wait for all sessions to finish
    for(auto thread : threads) {
        thread->join();
        delete thread;
    }
}

void Session::handle() {
    DisconnectHandler disconnectHandler(*this, handler);
    Socketbuf sbuf(socket);
    std::istream is(&sbuf);
    FixMessage msg;
    FixBuilder out;
    try {
        while (true) {
            FixMessage::parse(is, msg, GroupDefs());
            if (is.eof()) return;

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
            auto targetCompId = msg.getString(tagValue(Tags::TARGET_COMP_ID));
            if (targetCompId != config.senderCompId) {
                std::cerr << "rejecting connection, invalid target comp id " << targetCompId << ", expected " << config.senderCompId << "\n";
                Logout::build(out, "invalid target comp id");
                sendMessage(Logout::msgType, out);
                return;
            }

            auto senderCompId = msg.getString(tagValue(Tags::SENDER_COMP_ID));
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

void Initiator::connect() {
    if ((socket = ::socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return;
    }
    if (::connect(socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("socket connect");
        return;
    }
    session = new Session(socket, *this, config);
    connected=true;
    onConnected();
}

void Initiator::handle() {
    session->handle();
}

void Initiator::disconnect() {
    close(socket);
    connected = false;
}