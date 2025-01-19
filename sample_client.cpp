#include <chrono>

#include "fix_engine.h"
#include "msg_logon.h"
#include "msg_massquote.h"
#include <netdb.h>

struct SessionConfig sessionConfig("CLIENT","SERVER");

class MyClient : public Initiator {
  static const int N_QUOTES = 100000;

  FixBuilder fix;

  F bidPrice = 100.0;
  F askPrice = 101.0;
  F bidQty = 10;
  F askQty = 10;
  std::string symbol = "IBM";
  long quotes = 0;
  std::chrono::time_point<std::chrono::system_clock> start;

public:
  MyClient(sockaddr_in &server) : Initiator(server,sessionConfig){};
  void onConnected() {
    std::cout << "client connected!\n";
    Logon::build(fix);
    sendMessage(Logon::msgType,fix);
  }
  void onMessage(Session &session, const FixMessage &msg) {
    if(msg.msgType()==MassQuoteAck::msgType) {
        double adjust = rand() % 1 == 1 ? 1 : -1;
        bidPrice = bidPrice + adjust;
        askPrice = askPrice + adjust;
        MassQuote::build(fix,symbol,bidPrice,bidQty,askPrice,askQty);
        sendMessage(MassQuote::msgType,fix);
        if(++quotes%100000==0) {
            auto end = std::chrono::system_clock::now();
            auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            std::cout << "round-trip " << N_QUOTES << " quotes, usec per quote "
                << (duration.count() / (double)(N_QUOTES)) << ", quotes per sec "
                << (int)(((N_QUOTES) / (duration.count() / 1000000.0))) << "\n";

            start = std::chrono::system_clock::now();
        }
    }
  }
  bool validateLogon(const FixMessage& logon) { return true; }
  void onLoggedOn(const Session &session){
    std::cout << "client logged in!\n";

    start = std::chrono::system_clock::now();
    MassQuote::build(fix,symbol,bidPrice,bidQty,askPrice,askQty);
    sendMessage(MassQuote::msgType,fix);
  }
  void onLoggedOut(const Session &session,const std::string_view& text){
    std::cout << "client logged out "<<text<<"\n";
  }
};

int main(int argc, char *argv[]) {

  struct hostent *he;
  struct sockaddr_in server;

  const char hostname[] = "localhost";

  /* resolve hostname */
  if ((he = gethostbyname(hostname)) == NULL) {
    exit(1); /* error */
  }

  memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
  server.sin_port = htons(9000);
  server.sin_family = AF_INET;

  MyClient client(server);
  client.connect();
  client.handle();
}