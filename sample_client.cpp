#include <netdb.h>

#include <chrono>

#include "fix_engine.h"
#include "msg_logon.h"
#include "msg_massquote.h"

#define GO_TRADER

#ifdef GO_TRADER
static const char *TARGET_COMP_ID = "GOX";  // GOX to talk with go-trader, or SERVER to talk with sample_server
static const int PORT = 5001;               // 5001 to talk with go-trader, or 9000 to talk with sample_server
#else
static const char *TARGET_COMP_ID = "SERVER";  // GOX to talk with go-trader, or SERVER to talk with sample_server
static const int PORT = 9000;                  // 5001 to talk with go-trader, or 9000 to talk with sample_server
#endif

class MyClient : public Initiator {
    static const int N_QUOTES = 100000;

    FixBuilder fix;

    F bidPrice = 100.0;
    F askPrice = 101.0;
    F bidQty = 10;
    F askQty = 10;
    std::string symbol;
    long quotes = 0;
    std::chrono::time_point<std::chrono::system_clock> start;

   public:
    MyClient(sockaddr_in &server,std::string symbol,SessionConfig sessionConfig) : Initiator(server, sessionConfig), symbol(symbol) {};
    void onConnected() {
        std::cout << "client connected!, sending logon\n";
        Logon::build(fix);
        sendMessage(Logon::msgType, fix);
    }
    void onMessage(Session &session, const FixMessage &msg) {
        if (msg.msgType() == MassQuoteAck::msgType) {
            double adjust = rand() % 2 == 0 ? 0.01 : -0.01;

            if (bidPrice <= 25) adjust = 0.01;
            if (bidPrice >= 225) adjust = -0.01;

            bidPrice = bidPrice + adjust;
            askPrice = askPrice + adjust;

            MassQuote::build(fix, "MyQuote", "MyQuoteEntry",symbol, bidPrice, bidQty, askPrice, askQty);
            sendMessage(MassQuote::msgType, fix);
            if (++quotes % 100000 == 0) {
                auto end = std::chrono::system_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                std::cout << "round-trip " << N_QUOTES << " " << symbol << " quotes, usec per quote "
                          << (duration.count() / (double)(N_QUOTES)) << ", quotes per sec "
                          << (int)(((N_QUOTES) / (duration.count() / 1000000.0))) << "\n";

                start = std::chrono::system_clock::now();
            }
        }
    }
    bool validateLogon(const FixMessage &logon) { return true; }
    void onLoggedOn(const Session &session) {
        std::cout << "client logged in!\n";

        start = std::chrono::system_clock::now();
        MassQuote::build(fix, "MyQuote","MyQuoteEntry",symbol, bidPrice, bidQty, askPrice, askQty);
        sendMessage(MassQuote::msgType, fix);
    }
    void onLoggedOut(const Session &session, const std::string_view &text) {
        std::cout << "client logged out " << text << "\n";
    }
};

int main(int argc, char *argv[]) {
    struct hostent *he;
    struct sockaddr_in server;

    std::string symbol = "IBM";

    if(argc > 1 && strcmp(argv[1],"-h")==0) {
        std::cout << "usage: " << argv[0] << " [hostname] [symbol]\n";
        exit(0);
    }

    const char *hostname = "localhost";
    if (argc > 1) {
        hostname = argv[1];
    }
    if(argc > 2) {
        symbol = argv[2];
    }

    /* resolve hostname */
    if ((he = gethostbyname(hostname)) == NULL) {
        std::cerr << "gethostbyname failed" << hostname << "\n";
        exit(1); /* error */
    }

    memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
    server.sin_port = htons(PORT);
    server.sin_family = AF_INET;

    struct SessionConfig sessionConfig("CLIENT_"+symbol, TARGET_COMP_ID);

    MyClient client(server,symbol,sessionConfig);
    client.connect();
    client.handle();
}