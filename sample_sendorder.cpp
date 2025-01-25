#include <netdb.h>

#include <chrono>
#include <iostream>

#include "fix_engine.h"
#include "msg_logon.h"
#include "msg_orders.h"

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

struct Config {
    std::string symbol;
    F price;
    int quantity;
    int timeout_seconds;
};

class MyClient : public Initiator {
    static const int N_QUOTES = 100000;

    FixBuilder fix;
    Config config;

   public:
    MyClient(sockaddr_in &server,SessionConfig sessionConfig,Config config) : Initiator(server, sessionConfig), config(config) {};
    void onConnected() {
        std::cout << "client connected!, sending logon\n";
        Logon::build(fix);
        sendMessage(Logon::msgType, fix);
    }
    void onMessage(Session &session, const FixMessage &msg) {
        if (msg.msgType() == ExecutionReport::msgType) {
            std::cout << "received execution report:" << msg << "\n";
            if(msg.getInt(Tag::ORD_STATUS)==int(OrderStatus::Filled)) {
                std::cout << "status: order filled\n";
            }
            if(msg.getChar(20)==char(ExecType::Filled)) {
                std::cout << "trade: order filled\n";
                disconnect();
            }
            if(msg.getInt(Tag::ORD_STATUS)==int(OrderStatus::Canceled)) {
                std::cout << "status: order cancelled\n";
                disconnect();
            }
        }
    }
    bool validateLogon(const FixMessage &logon) { return true; }
    void onLoggedOn(const Session &session) {
        std::cout << "client logged in!\n";
        std::cout << "sending buy order: " << config.symbol << " " << config.price << " " << config.quantity << "\n";

        NewOrderSingle::build<7>(fix, config.symbol, OrderType::Limit, OrderSide::Buy, config.price, config.quantity, "MyOrder");
        sendMessage(NewOrderSingle::msgType, fix);
    }
    void onLoggedOut(const Session &session, const std::string_view &text) {
        std::cout << "client logged out " << text << "\n";
    }
};

int main(int argc, char *argv[]) {
    struct hostent *he;
    struct sockaddr_in server;

    if((argc > 1 && strcmp(argv[1],"-h")==0) || argc != 6) {
        std::cout << "usage: " << argv[0] << " hostname symbol price quantity timeout_seconds\n";
        exit(0);
    }
    const char *hostname = argv[1];

    Config config = {
        argv[2],
        std::stod(argv[3]),
        std::stoi(argv[4]),
        std::stoi(argv[5])
    };

    /* resolve hostname */
    if ((he = gethostbyname(hostname)) == NULL) {
        std::cerr << "gethostbyname failed" << hostname << "\n";
        exit(1); /* error */
    }

    memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
    server.sin_port = htons(config::PORT);
    server.sin_family = AF_INET;

    struct SessionConfig sessionConfig("SENDORDER_"+config.symbol, config::TARGET_COMP_ID);

    MyClient client(server, sessionConfig, config);
    std::thread clientThread([&client]() {
        client.connect();
        client.handle();
    });

    auto start = std::chrono::system_clock::now();
    while(true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if(!client.isConnected()) {
            break;
        }
        auto now = std::chrono::system_clock::now();
        if(std::chrono::duration_cast<std::chrono::seconds>(now-start).count() > config.timeout_seconds) {
            std::cout << "Timeout reached, disconnecting client\n";
            client.disconnect();
            break;
        }
    }
    clientThread.join();
}