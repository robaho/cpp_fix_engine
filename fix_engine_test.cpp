#include <netinet/in.h>
#include <iostream>
#include <thread>
#include "fix_builder.h"
#define BOOST_TEST_MODULE fix_engine_test
#include <boost/test/included/unit_test.hpp>

#include "fix_engine.h"
#include "msg_logon.h"

BOOST_AUTO_TEST_CASE( disconnect ) {
    class TestAcceptor : public Acceptor<> {
    public:
        TestAcceptor(int port, const DefaultSessionConfig& config) : Acceptor(port, config) {}
        void onMessage(Session<>& session, const FixMessage& msg) override {}
        bool validateLogon(const FixMessage& logon) override { return true; }
        void onDisconnected(const Session<>& session) override {
            std::cout << "received session disconnect\n";
            BOOST_TEST(session.id()=="server:*");
            Acceptor::onDisconnected(session);
            shutdown();
            std::cout << "disconnected server socket\n";
        }
    };

    TestAcceptor acceptor(9001, DefaultSessionConfig("server", "*"));
    auto t = std::thread([&acceptor](){
        acceptor.listen();
        std::cout << "acceptor listen() finished\n";
    });

    // give time for acceptor to start
    std::this_thread::sleep_for(std::chrono::seconds(1));

    class TestInitiator : public Initiator<> {
    public:
        TestInitiator(const sockaddr_in server, const DefaultSessionConfig& config) : Initiator(server, config) {}
        bool validateLogon(const FixMessage& logon) override { return true; }
    };

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(9001);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    TestInitiator initiator(server, DefaultSessionConfig("client", "server"));
    initiator.connect();
    BOOST_TEST(initiator.isConnected());
    initiator.disconnect();
    std::cout << "joining acceptor thread\n";
    t.join();
}

BOOST_AUTO_TEST_CASE( logon ) {
    std::cout << "----- logon test\n";
    class TestAcceptor : public Acceptor<> {
    public:
        TestAcceptor(int port, const DefaultSessionConfig& config) : Acceptor(port, config) {}
        void onMessage(Session<>& session, const FixMessage& msg) override {}
        bool validateLogon(const FixMessage& logon) override { return true; }
        void onLoggedOn(const Session<>& session) override {
            Acceptor::onLoggedOn(session);
            std::cout << "received logon\n";
            BOOST_TEST(session.id()=="server:client");
            shutdown();
        }
    };

    TestAcceptor acceptor(9001, DefaultSessionConfig("server", "*"));
    auto t = std::thread([&acceptor](){
        acceptor.listen();
        std::cout << "acceptor listen() finished\n";
    });

    // give time for acceptor to start
    std::this_thread::sleep_for(std::chrono::seconds(1));

    class TestInitiator : public Initiator<> {
    public:
        TestInitiator(const sockaddr_in server, const DefaultSessionConfig& config) : Initiator(server, config) {}
        bool validateLogon(const FixMessage& logon) override { return true; }
        void onConnected() override {
            std::cout << "sending logon\n";
            FixBuilder msg;
            Initiator::onConnected();
            Logon::build(msg);
            sendMessage(Logon::msgType, msg);
        }
    };

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(9001);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    TestInitiator initiator(server, DefaultSessionConfig("client", "server"));
    initiator.connect();
    BOOST_TEST(initiator.isConnected());
    std::cout << "joining acceptor\n";
    t.join();
    std::cout << "disconnecting initiator\n";
    initiator.disconnect();
    std::cout << "disconnected client socket\n";
}

