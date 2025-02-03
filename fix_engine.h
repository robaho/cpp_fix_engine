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

struct DefaultSessionConfig {
    std::string beginString = "FIX.4.4";
    std::string senderCompId;
    std::string targetCompId;
    int nextSeqNum = 1;
    int expectedSeqNum = 1;

    DefaultSessionConfig(std::string senderCompId, std::string targetCompId) : senderCompId(senderCompId), targetCompId(targetCompId) {}

    // initialize header fields, except fields 8,9,35 which are maintained by the engine
    void configureHeader(FixBuilder& msg) {
        msg.addField(Tag::SENDER_COMP_ID, senderCompId);
        msg.addField(Tag::TARGET_COMP_ID, targetCompId);
        msg.addField(Tag::SEQ_NUM, nextSeqNum++);
    }

    // initialize the configuration from the logon message. afterwhich the id() must remain constant
    void initialize(const FixMessage& logon) {
    }

    std::string id() const {
        return senderCompId + ":" + targetCompId;
    }
};

template <class SessionConfig=DefaultSessionConfig>
class Session;

template <class SessionConfig=DefaultSessionConfig>
struct SessionHandler {
    virtual void onMessage(Session<SessionConfig>& session, const FixMessage& msg) = 0;
    virtual bool validateLogon(const FixMessage& logon) = 0;
    virtual void onDisconnected(const Session<SessionConfig>& session) = 0;
    virtual void onLoggedOn(const Session<SessionConfig>& session) = 0;
};


inline std::ostream& operator<<(std::ostream& os, const DefaultSessionConfig& config) {
    return os << "[" << config.id() << " seq " << config.nextSeqNum << "]";
}

template <class SessionConfig>
class Acceptor;

template <class SessionConfig>
class Initiator;

template <class SessionConfig>
class Session : ParkSupport {
    friend class Acceptor<SessionConfig>;
    friend class Initiator<SessionConfig>;
    bool loggedIn = false;
    const int socket;
    void handle();
    FixBuilder fullMsg;
    SessionHandler<SessionConfig>& handler;
    std::mutex lock;
    // thread is owned by the session and reads the socket via handle()
    boost::fibers::fiber* fiber = nullptr;
    Socketbuf sbuf;
    std::ostream os;

   protected:
    SessionConfig config;
    Session(int socket, SessionHandler<SessionConfig>& handler, SessionConfig config) : socket(socket), handler(handler), sbuf(socket, *this), os(&sbuf), config(config) {}

    struct DisconnectHandler {
        Session& session;
        SessionHandler<SessionConfig>& handler;
        DisconnectHandler(Session& session, SessionHandler<SessionConfig>& handler) : session(session), handler(handler) {}
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
        config.configureHeader(fullMsg);
        fullMsg.addBuilder(msg);
        fullMsg.writeTo(os);
        os.flush();
    }
    std::string id() const {
        return config.id();
    }
};

template <class SessionConfig=DefaultSessionConfig>
class Acceptor : public SessionHandler<SessionConfig> {
    const int port;
    int serverSocket;
    std::shared_mutex sessionLock;
    std::map<std::string, Session<SessionConfig>*> sessionMap;
    SessionConfig config;
    Poller poller;
    int workerThreads;

   protected:
   public:
    Acceptor(int port, SessionConfig config, int workerThreads=2) : port(port), config(config), workerThreads(workerThreads) {}
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& sessionId, const std::string& msgType, FixBuilder& msg) {
        Session<SessionConfig>* session;
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
    virtual void onDisconnected(const Session<SessionConfig>& session) {
        poller.remove_socket(session.socket);
        std::unique_lock<std::shared_mutex> mu(sessionLock);
        sessionMap.erase(session.config.id());
    }
    virtual void onLoggedOn(const Session<SessionConfig>& session) {
        std::unique_lock<std::shared_mutex> mu(sessionLock);
        sessionMap[session.config.id()] = const_cast<Session<SessionConfig>*>(&session);
    }
    // Listen for initiators. Function does not return until shutdown() is called.
    void listen();
    // Shutdown the acceptor. This will close the server socket.
    void shutdown() {
        close(serverSocket);
    }
};

template <class SessionConfig=DefaultSessionConfig>
class Initiator : public SessionHandler<SessionConfig> {
    bool connected = false;
    const sockaddr_in server;
    Session<SessionConfig>* session = nullptr;
    const SessionConfig config;
    int socket;
   protected:
    Poller *poller;

   public:
    Initiator(struct sockaddr_in server, const SessionConfig config, Poller* poller = nullptr) : server(server), config(config), poller(poller) {}
    virtual ~Initiator() {
        if (session && session->fiber) session->fiber->join();
        if (session) delete session;
    }
    // The message should be sent should not contain any of the header or trailer fields.
    // The msg is automatically reset.
    void sendMessage(const std::string& msgType, FixBuilder& msg) {
        session->sendMessage(msgType, msg);
    }
    void connect();
    bool isConnected() {
        return connected;
    }
    void disconnect() {
        close(socket);
        connected = false;
    }
    void handle() {
        if(poller) {
            auto fiber = new boost::fibers::fiber(&Session<SessionConfig>::handle, session);
            session->fiber = fiber;
            return;
        } else {
            session->handle();
        }
    }
    virtual void onConnected() {}
    virtual void onDisconnected(const Session<SessionConfig>& session) {
        if(poller) poller->remove_socket(socket);
    }
    virtual void onLoggedOn(const Session<SessionConfig>& session) {}
    virtual void onLoggedOut(const Session<SessionConfig>& session, const std::string_view& text) {}
    virtual void onMessage(Session<SessionConfig>& session, const FixMessage& msg) {}
};