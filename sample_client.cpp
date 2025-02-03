#include <netdb.h>
#include <latch>

#include <chrono>
#include <stdexcept>
#include <algorithm>

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

static std::atomic<long> quoteCount = 0;

class MyClient : public Initiator<> {
    static const int N_QUOTES = 10000;

    FixBuilder fix;

    F bidPrice = 100.0;
    F askPrice = 101.0;
    F bidQty = 10;
    F askQty = 10;
    std::string symbol;
    std::chrono::time_point<std::chrono::system_clock> start;
    std::latch& latch;

   public:
    MyClient(sockaddr_in &server,std::string symbol,DefaultSessionConfig sessionConfig,std::latch& latch,Poller* poller=nullptr) : Initiator(server, sessionConfig, poller), symbol(symbol), latch(latch) {};
    void onConnected() override {
        std::cout << "client connected!, sending logon\n";
        Logon::build(fix);
        sendMessage(Logon::msgType, fix);
    }
    void onMessage(Session<> &session, const FixMessage &msg) override {
        if (msg.msgType() == MassQuoteAck::msgType) {
            double adjust = rand() % 2 == 0 ? 0.01 : -0.01;

            if (bidPrice <= 25) adjust = 0.01;
            if (bidPrice >= 225) adjust = -0.01;

            bidPrice = bidPrice + adjust;
            askPrice = askPrice + adjust;

            MassQuote::build(fix, "MyQuote", "MyQuoteEntry",symbol, bidPrice, bidQty, askPrice, askQty);
            sendMessage(MassQuote::msgType, fix);
            quoteCount+=1;
        }
    }
    bool validateLogon(const FixMessage &logon) override { return true; }
    void onLoggedOn(const Session<> &session) override {
        std::cout << "client logged in!\n";

        start = std::chrono::system_clock::now();
        MassQuote::build(fix, "MyQuote","MyQuoteEntry",symbol, bidPrice, bidQty, askPrice, askQty);
        sendMessage(MassQuote::msgType, fix);
    }
    void onLoggedOut(const Session<> &session, const std::string_view &text) override {
        std::cout << "client logged out " << text << "\n";
    }
    void onDisconnected(const Session<>& session) override {
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
        int workerThreads = std::max(int(std::thread::hardware_concurrency()) / 2,1);

        std::vector<std::thread> workers;

        boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>();

        typedef boost::fibers::buffered_channel<std::string> channel_t;
        channel_t chan{2};

        std::latch latch(benchCount);

        for(int i=0;i<workerThreads;i++) {
            workers.push_back(
            std::thread([&chan,&server,&poller,&latch,workerThreads]() {
                boost::fibers::use_scheduling_algorithm<boost::fibers::algo::work_stealing>(workerThreads,true);
                while(true) {
                    std::string symbol;
                    if(chan.pop(symbol)!=boost::fibers::channel_op_status::success) {
                        return;
                    }
                    struct DefaultSessionConfig sessionConfig("CLIENT_"+symbol, config::TARGET_COMP_ID);
                    auto client = new MyClient(server,symbol,sessionConfig,latch,&poller);
                    client->connect();
                    if(client->isConnected()) {
                        std::cout << "client connected\n";
                        client->handle();
                    }
                }
            }));
        }
        auto reporter = boost::fibers::fiber(
        [&latch]() {
            while(!latch.try_wait()) {
                auto start = std::chrono::system_clock::now();
                long startCount = quoteCount;
                boost::this_fiber::sleep_for(std::chrono::seconds(5));
                auto end = std::chrono::system_clock::now();
                long nQuotes = quoteCount-startCount;
                auto duration =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                std::cout << "round-trip " << nQuotes << " quotes, usec per quote "
                          << (duration.count() / (double)(nQuotes)) << ", quotes per sec "
                          << (int)(((nQuotes) / (duration.count() / 1000000.0))) << "\n";

            }
        });

        for(int i=0;i<benchCount;i++) {
            auto symbol = std::string("S")+std::to_string(i);
            chan.push(symbol);
        }

        while(!latch.try_wait()) {
            boost::this_fiber::sleep_for(std::chrono::milliseconds(1000));
        }

        chan.close();
        reporter.join();
        
        poller.close();
        pollerThread.join();

        for(auto& worker : workers) worker.join();
        for(auto client : clients) delete client;
        std::cout << "all clients disconnected\n";
    } else {
        std::latch latch(1);
        auto reporter = std::thread([&latch,symbol]() {
            while(!latch.try_wait()) {
                auto start = std::chrono::system_clock::now();
                long startCount = quoteCount;
                boost::this_fiber::sleep_for(std::chrono::seconds(5));
                auto end = std::chrono::system_clock::now();
                long nQuotes = quoteCount-startCount;
                auto duration =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                std::cout << "round-trip " << nQuotes << " quotes on " << symbol << ", usec per quote "
                          << (duration.count() / (double)(nQuotes)) << ", quotes per sec "
                          << (int)(((nQuotes) / (duration.count() / 1000000.0))) << "\n";
            }
        });
        struct DefaultSessionConfig sessionConfig("CLIENT_"+symbol, config::TARGET_COMP_ID);
        MyClient client(server,symbol,sessionConfig,latch);
        client.connect();
        if(client.isConnected()) {
            std::cout << "client connected\n";
            client.handle();
        }
        reporter.join();
    }
}