#include <netdb.h>
#include <latch>

#include <chrono>
#include <stdexcept>

#include "fix_engine.h"
#include "msg_logon.h"
#include "msg_massquote.h"

// #define GO_TRADER

namespace config {
#ifdef GO_TRADER
static const char *TARGET_COMP_ID = "GOX";  // GOX to talk with go-trader, or SERVER to talk with sample_server
static const int PORT = 5001;               // 5001 to talk with go-trader, or 9000 to talk with sample_server
#else
static const char *TARGET_COMP_ID = "SERVER";  // GOX to talk with go-trader, or SERVER to talk with sample_server
static const int PORT = 9000;                  // 5001 to talk with go-trader, or 9000 to talk with sample_server
#endif
}

class MyClient : public Initiator {
    static const int N_QUOTES = 10000;

    FixBuilder fix;

    F bidPrice = 100.0;
    F askPrice = 101.0;
    F bidQty = 10;
    F askQty = 10;
    std::string symbol;
    long quotes = 0;
    std::chrono::time_point<std::chrono::system_clock> start;
    std::latch& latch;

   public:
    MyClient(sockaddr_in &server,std::string symbol,SessionConfig sessionConfig,std::latch& latch) : Initiator(server, sessionConfig), symbol(symbol),latch(latch) {};
    void onConnected() override {
        std::cout << "client connected!, sending logon\n";
        Logon::build(fix);
        sendMessage(Logon::msgType, fix);
    }
    void onMessage(Session &session, const FixMessage &msg) override {
        if (msg.msgType() == MassQuoteAck::msgType) {
            double adjust = rand() % 2 == 0 ? 0.01 : -0.01;

            if (bidPrice <= 25) adjust = 0.01;
            if (bidPrice >= 225) adjust = -0.01;

            bidPrice = bidPrice + adjust;
            askPrice = askPrice + adjust;

            MassQuote::build(fix, "MyQuote", "MyQuoteEntry",symbol, bidPrice, bidQty, askPrice, askQty);
            sendMessage(MassQuote::msgType, fix);
            if (++quotes % N_QUOTES == 0) {
                auto end = std::chrono::system_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                std::cout << "round-trip " << N_QUOTES << " " << symbol << " quotes, usec per quote "
                          << (duration.count() / (double)(N_QUOTES)) << ", quotes per sec "
                          << (int)(((N_QUOTES) / (duration.count() / 1000000.0))) 
                          << ", last spread " << bidPrice << "/" << askPrice << "\n";

                start = std::chrono::system_clock::now();
            }
        }
    }
    bool validateLogon(const FixMessage &logon) override { return true; }
    void onLoggedOn(const Session &session) override {
        std::cout << "client logged in!\n";

        start = std::chrono::system_clock::now();
        MassQuote::build(fix, "MyQuote","MyQuoteEntry",symbol, bidPrice, bidQty, askPrice, askQty);
        sendMessage(MassQuote::msgType, fix);
    }
    void onLoggedOut(const Session &session, const std::string_view &text) override {
        std::cout << "client logged out " << text << "\n";
    }
    void onDisconnected(const Session& session) override {
        latch.count_down();
        Initiator::onDisconnected(session);
    }
};

int main(int argc, char *argv[]) {
    struct hostent *he;
    struct sockaddr_in server;

    std::string symbol = "IBM";
    int benchCount = 0;

    if(argc > 1 && strcmp(argv[1],"-h")==0) {
        std::cout << "usage: " << argv[0] << " [hostname] [symbol]\n";
        exit(0);
    }

    const char *hostname = "localhost";
    if (argc > 1) {
        hostname = argv[1];
    }
    if(argc > 2) {
        if(strcmp("-bench",argv[2])==0) {
            benchCount = atoi(argv[3]);
        } else {
            symbol = argv[2];
        }
    }

    /* resolve hostname */
    if ((he = gethostbyname(hostname)) == NULL) {
        std::cerr << "gethostbyname failed" << hostname << "\n";
        exit(1); /* error */
    }

    memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
    server.sin_port = htons(config::PORT);
    server.sin_family = AF_INET;

    if(benchCount>0) {
        Poller poller;
        std::thread pollerThread([&poller]() {
            while (true) {
                try {
                    poller.poll();
                } catch(const std::runtime_error& err) {
                    std::cerr << "poller error: " << err.what() << "\n";
                    return;
                }
            }
        });
        std::vector<MyClient*> clients;
        boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>();
        std::latch latch(benchCount);
        for(int i=0;i<benchCount;i++) {
            auto symbol = std::string("S")+std::to_string(i);
            struct SessionConfig sessionConfig("CLIENT_"+symbol, config::TARGET_COMP_ID);
            auto client = new MyClient(server,symbol,sessionConfig,latch);
            clients.push_back(client);
            client->connect(true,&poller);
            std::cout << "client connected\n";
            client->handle(true);
        }
        while(!latch.try_wait()) {
            boost::this_fiber::sleep_for(std::chrono::milliseconds(1000));
        }

        poller.close();
        pollerThread.join();

        for(auto client : clients) delete client;
        std::cout << "all clients disconnected\n";
    } else {
        std::latch latch(1);
        struct SessionConfig sessionConfig("CLIENT_"+symbol, config::TARGET_COMP_ID);
        MyClient client(server,symbol,sessionConfig,latch);
        client.connect();
        std::cout << "client connected\n";
        client.handle();
    }
}