#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "cpp_fix_codec/fix_parser.h"
#include "cpp_fix_codec/fix_builder.h"

class Session;

struct MessageHandler {
    virtual void onMessage(Session& session,const FixMessage& msg) = 0;
    virtual bool validateLogon(const FixMessage& logon) = 0;
};

struct SessionConfig {
    std::string beginString = "FIX.4.4";
    std::string senderCompId;
    std::string targetCompId;
    int nextSeqNum = 1;
    int expectedSeqNum = 1;

    SessionConfig(std::string senderCompId,std::string targetCompId) : senderCompId(senderCompId), targetCompId(targetCompId){}

    // initialize header fields, except 8,9,35
    virtual void initialize(FixBuilder& msg) {
        msg.addField(49, senderCompId);
        msg.addField(56, targetCompId);
        msg.addField(34,nextSeqNum++);
    }
};

inline std::ostream& operator<<(std::ostream& os, const SessionConfig& config)
{
    return os << "[" << config.senderCompId << "," << config.targetCompId << "]";
}

class Session {
friend class Acceptor;
friend class Initiator;
    int nextSeqIn = 0;
    int nextSeqOut = 0;
    bool loggedIn;
    const int socket;
    void handle();
    MessageHandler& handler;
    SessionConfig config;
    FixBuilder fullMsg;
    std::mutex lock;
    Session(int socket, MessageHandler& handler, SessionConfig config) : socket(socket), handler(handler), config(config) {}
public: 
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& msgType,FixBuilder& msg){
        std::lock_guard<std::mutex> mu(lock);
        fullMsg.addField(8,config.beginString);
        fullMsg.addField(9,"0000");
        fullMsg.addField(35, msgType);
        fullMsg.addTimeNow(52);
        config.initialize(fullMsg);
        fullMsg.addBuilder(msg);
        fullMsg.writeTo(socket);
    }
};

class Acceptor : public MessageHandler {
    const int port;
    int serverSocket;
    std::vector<std::thread> threads;
    SessionConfig config;
public:
    Acceptor(int port,SessionConfig config) : port(port),config(config){}
    virtual void onConnected(struct sockaddr_in remote){}
    virtual void onDisconnected(const Session& session,struct sockaddr_in remote){}
    // Listen for initiators. Function does not return.
    void listen();
};

class Initiator : public MessageHandler {
    int socket;
    const sockaddr_in server;
    Session *session = nullptr;
    SessionConfig config;
    FixBuilder fullMsg;
    std::mutex lock;
public:
    Initiator(struct sockaddr_in server,SessionConfig config) : server(server), config(config){}
    ~Initiator() {
        if(session!=nullptr) delete session;
    }
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& msgType,FixBuilder& msg){
        std::lock_guard<std::mutex> mu(lock);
        fullMsg.addField(8,config.beginString);
        fullMsg.addField(9,"0000");
        fullMsg.addField(35, msgType);
        fullMsg.addTimeNow(52);
        config.initialize(fullMsg);
        fullMsg.addBuilder(msg);
        fullMsg.writeTo(socket);
    }
    void connect();
    void handle();
    virtual void onConnected(){}
    virtual void onDisconnected(){}
    virtual void onLoggedOn(const Session& session){}
    virtual void onLoggedOut(const Session& session,const std::string_view& text){}
    virtual void onMessage(Session& session, const FixMessage& msg){}
};