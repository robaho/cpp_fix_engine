#include "fix_engine.h"
#include "msg_massquote.h"

class MyServer : public Acceptor {
public:
    MyServer() : Acceptor(9000,SessionConfig("SERVER","*")){};
    void onMessage(Session& session,const FixMessage& msg) {
        if(msg.msgType()==MassQuote::msgType) {
            FixBuilder fix(256);
            MassQuoteAck::build(fix,msg.getString(117),0);
            session.sendMessage(MassQuoteAck::msgType,fix);
        }
    }
    bool validateLogon(const FixMessage& msg) {
        return true;
    }
};

int main(int argc, char* argv[]) {
    MyServer server;
    server.listen();
}