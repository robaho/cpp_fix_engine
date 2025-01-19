#include "cpp_fix_codec/fix.h"
#include "socketbuf.h"
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>

#include "fix_engine.h"
#include "msg_logon.h"
#include "msg_logout.h"

void Acceptor::listen() {
  if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    return;
  }
  struct sockaddr_in serverAddress;

  serverAddress.sin_family = AF_INET;
  serverAddress.sin_addr.s_addr = INADDR_ANY;
  serverAddress.sin_port = htons(port);

  // 3. Bind the socket to the address
  if (bind(serverSocket, (struct sockaddr *)&serverAddress,
           sizeof(serverAddress)) < 0) {
    std::cerr << "Error binding socket." << std::endl;
    return;
  }

  while (true) {
    std::cout << "listening for connections... on port " << port << "\n";
    if (::listen(serverSocket, 5) < 0) { // 5 is the backlog size
      std::cerr << "Error listening." << std::endl;
      return;
    }
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    char ip_str[INET_ADDRSTRLEN];

    int clientSocket =
        accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (clientSocket < 0) {
      std::cerr << "Error accepting connection." << std::endl;
    } else {
      inet_ntop(AF_INET, &(clientAddr.sin_addr), ip_str, INET_ADDRSTRLEN);
      std::cout << "connection from " << ip_str << " port "
                << ntohs(clientAddr.sin_port) << "\n";
      try {
        onConnected(clientAddr);
        auto session = new Session(clientSocket, *this, config);
        // use shared for Session so that when thread terminates the Session can
        // be cleaned-up
        threads.push_back(std::thread(&Session::handle, session));
      } catch (std::runtime_error &err) {
        std::cerr << "Acceptor refused connection: " << err.what() << "\n";
      }
    }
  }
}

void Session::handle() {
  Socketbuf sbuf(socket);
  std::istream is(&sbuf);
  FixMessage msg;
  FixBuilder out;
  try {

    while (true) {
      FixMessage::parse(is, msg, GroupDefs());
      if(is.eof()) return;

      if (!loggedIn && msg.msgType() != Logon::msgType) {
        std::cerr << "rejecting connection, " << msg.msgType()
                  << " is not a Logon\n";
        Logout::build(out, "not logged in");
        sendMessage(Logout::msgType, out);
        return;
      }
      if (msg.seqNum() != config.expectedSeqNum) {
        std::cerr << "rejecting connection, " << msg.seqNum() << " != expected "
                  << config.expectedSeqNum << "\n";
        Logout::build(out, "invalid sequence number");
        sendMessage(Logout::msgType, out);
        return;
      }
      if (msg.getString(tagValue(Tags::TARGET_COMP_ID)) !=
          config.senderCompId) {
        std::cerr << "rejecting connection, invalid target comp id\n";
        Logout::build(out, "invalid target comp id");
        sendMessage(Logout::msgType, out);
        return;
      }
      if (msg.getString(tagValue(Tags::SENDER_COMP_ID)) != config.targetCompId) {
        if (config.targetCompId == "") {
          config.targetCompId = msg.getString(tagValue(Tags::SENDER_COMP_ID));
        } else {
          std::cerr << "rejecting connection, invalid sender comp id\n";
          Logout::build(out, "invalid sender comp id");
          sendMessage(Logout::msgType, out);
          return;
        }
      }
      if (!loggedIn) {
        if (!handler.validateLogon((msg))) {
          std::cerr << "logon rejected\n";
          Logout::build(out, "invalid logon");
          sendMessage(Logout::msgType, out);
          return;
        }
        Logon::build(out);
        sendMessage(Logon::msgType, out);
        loggedIn = true;
      }
      handler.onMessage(*this, msg);
      config.expectedSeqNum++;
    }
  } catch (const std::runtime_error &err) {
    std::cerr << "exception processing session: " << config << ", " << err.what() << "\n";
    return;
  }
}

void Initiator::connect() {
  if ((socket = ::socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    return;
  }
  if (::connect(socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("socket connect");
    return;
  }
  session = new Session(socket, *this, SessionConfig("", ""));
  onConnected();
}

void Initiator::handle() {
  Socketbuf sbuf(socket);
  std::istream is(&sbuf);
  FixMessage msg;
  while (!is.eof()) {
    FixMessage::parse(is, msg, GroupDefs());
    // std::cout << "client received message! type "<<msg.msgType()<<", eof "<<is.eof()<<"\n";
    if (msg.msgType() == Logon::msgType) {
      onLoggedOn(*session);
      session->loggedIn = true;
    }
    if (msg.msgType() == Logout::msgType) {
      onLoggedOut(*session, msg.getString(tagValue(Tags::TEXT)));
      session->loggedIn = false;
    }
    onMessage(*session, msg);
    config.expectedSeqNum++;
  }
}