#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

#include "fix_builder.h"
#include "fix_parser.h"

class Session;

struct SessionHandler {
    virtual void onMessage(Session& session, const FixMessage& msg) = 0;
    virtual bool validateLogon(const FixMessage& logon) = 0;
    virtual void onDisconnected(const Session& session) = 0;
    virtual void onLoggedOn(const Session& session) = 0;
};

struct SessionConfig {
    std::string beginString = "FIX.4.4";
    std::string senderCompId;
    std::string targetCompId;
    int nextSeqNum = 1;
    int expectedSeqNum = 1;

    SessionConfig(std::string senderCompId, std::string targetCompId) : senderCompId(senderCompId), targetCompId(targetCompId) {}

    // initialize header fields, except 8,9,35
    virtual void initialize(FixBuilder& msg) {
        msg.addField(Tag::SENDER_COMP_ID, senderCompId);
        msg.addField(Tag::TARGET_COMP_ID, targetCompId);
        msg.addField(Tag::SEQ_NUM, nextSeqNum++);
    }

    std::string id() const {
        return senderCompId+":"+targetCompId;
    }
};

inline std::ostream& operator<<(std::ostream& os, const SessionConfig& config) {
    return os << "[" << config.id() << " seq " << config.nextSeqNum << "]";
}

class Acceptor;

class Session {
    friend class Acceptor;
    friend class Initiator;
    bool loggedIn=false;
    const int socket;
    void handle();
    FixBuilder fullMsg;
    SessionHandler& handler;
    std::mutex lock;
    // thread is owned by the session and reads the socket via handle()
    std::thread* thread = nullptr;
    
protected:
    SessionConfig config;
    Session(int socket, SessionHandler& handler, SessionConfig config) : socket(socket), handler(handler), config(config) {}

    struct DisconnectHandler {
        Session &session;
        SessionHandler& handler;
        DisconnectHandler(Session &session, SessionHandler& handler) : session(session), handler(handler) {}
        ~DisconnectHandler() {
            handler.onDisconnected(session);
        }
    };

   public:
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& msgType, FixBuilder& msg) {
        std::lock_guard<std::mutex> mu(lock);
        fullMsg.addField(8, config.beginString);
        fullMsg.addField(9, "0000");
        fullMsg.addField(35, msgType);
        fullMsg.addTimeNow(52);
        config.initialize(fullMsg);
        fullMsg.addBuilder(msg);
        fullMsg.writeTo(socket);
    }
    std::string id() const {
        return config.id();
    }
};

class Acceptor : public SessionHandler {
    const int port;
    int serverSocket;
    std::shared_mutex sessionLock;
    std::map<std::string, Session*> sessionMap;
    std::vector<std::thread*> threads;
    SessionConfig config;
    void put(Session* session) {
        std::unique_lock<std::shared_mutex> mu(sessionLock);
        sessionMap[session->config.id()] = session;
    }
    protected:

   public:
    Acceptor(int port, SessionConfig config) : port(port), config(config) {}
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& sessionId, const std::string& msgType, FixBuilder& msg) {
        Session* session;
        {
            std::shared_lock<std::shared_mutex> mu(sessionLock);
            session = sessionMap[sessionId];
        }
        if (session != nullptr) {
            session->sendMessage(msgType, msg);
        } else {
            std::cerr << "Session not found for " << sessionId << "\n";
        }
    }
    virtual void onConnected(struct sockaddr_in remote) {}
    virtual void onDisconnected(const Session& session) {}
    virtual void onLoggedOn(const Session& session) {
        put(const_cast<Session*>(&session));
    }
    // Listen for initiators. Function does not return until shutdown() is called.
    void listen();
    // Shutdown the acceptor. This will close the server socket.
    void shutdown() {
        close(serverSocket);
    }
};

class Initiator : public SessionHandler {
    int socket;
    bool connected = false;
    const sockaddr_in server;
    Session* session = nullptr;
    const SessionConfig config;

   public:
    Initiator(struct sockaddr_in server, const SessionConfig config) : server(server), config(config) {}
    ~Initiator() {
        if (session != nullptr) delete session;
    }
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& msgType, FixBuilder& msg) {
        session->sendMessage(msgType,msg);
    }
    void connect();
    bool isConnected() {
        return connected;
    }
    void disconnect();
    void handle();
    virtual void onConnected(){}
    virtual void onDisconnected(const Session& session) {}
    virtual void onLoggedOn(const Session& session) {}
    virtual void onLoggedOut(const Session& session, const std::string_view& text) {}
    virtual void onMessage(Session& session, const FixMessage& msg) {}
};