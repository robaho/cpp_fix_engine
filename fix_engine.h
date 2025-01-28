#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <boost/fiber/all.hpp>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "fix_builder.h"
#include "fix_parser.h"
#include "park_unpark.h"
#include "poller.h"
#include "socketbuf.h"

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
        return senderCompId + ":" + targetCompId;
    }
};

inline std::ostream& operator<<(std::ostream& os, const SessionConfig& config) {
    return os << "[" << config.id() << " seq " << config.nextSeqNum << "]";
}

class Acceptor;

class Session : ParkSupport {
    friend class Acceptor;
    friend class Initiator;
    bool loggedIn = false;
    const int socket;
    void handle();
    FixBuilder fullMsg;
    SessionHandler& handler;
    std::mutex lock;
    // thread is owned by the session and reads the socket via handle()
    boost::fibers::fiber* fiber = nullptr;
    Socketbuf sbuf;
    std::ostream os;

   protected:
    SessionConfig config;
    Session(int socket, SessionHandler& handler, SessionConfig config) : socket(socket), handler(handler), sbuf(socket, *this), os(&sbuf), config(config) {}

    struct DisconnectHandler {
        Session& session;
        SessionHandler& handler;
        DisconnectHandler(Session& session, SessionHandler& handler) : session(session), handler(handler) {}
        ~DisconnectHandler() {
            std::cout << "session disconnected " << session.id() << "\n";
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
        fullMsg.writeTo(os);
        os.flush();
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
    std::vector<boost::fibers::fiber*> fibers;
    SessionConfig config;
    void put(Session* session) {
        std::unique_lock<std::shared_mutex> mu(sessionLock);
        sessionMap[session->config.id()] = session;
    }
    Poller poller;

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
    // override to filter the incoming address. throw an exception to disallow the connection request.
    virtual void onConnected(struct sockaddr_in remote) {}
    virtual void onDisconnected(const Session& session) {
        poller.remove_socket(session.socket);
        std::unique_lock<std::shared_mutex> mu(sessionLock);
        sessionMap.erase(session.config.id());
    }
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
    bool connected = false;
    const sockaddr_in server;
    Session* session = nullptr;
    const SessionConfig config;
    int socket;
    Poller *poller;

   public:
    Initiator(struct sockaddr_in server, const SessionConfig config) : server(server), config(config) {}
    virtual ~Initiator() {
        if (session && session->fiber) session->fiber->join();
        if (session) delete session;
    }
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& msgType, FixBuilder& msg) {
        session->sendMessage(msgType, msg);
    }
    void connect(bool nonBlocking=false,Poller *poller=nullptr);
    bool isConnected() {
        return connected;
    }
    void disconnect();
    void handle(bool nonBlocking=false);
    virtual void onConnected() {}
    virtual void onDisconnected(const Session& session) {
        if(poller) poller->remove_socket(socket);
    }
    virtual void onLoggedOn(const Session& session) {}
    virtual void onLoggedOut(const Session& session, const std::string_view& text) {}
    virtual void onMessage(Session& session, const FixMessage& msg) {}
};