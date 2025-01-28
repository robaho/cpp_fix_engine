#pragma once

#include <cerrno>
#include <iostream>
#include <unistd.h>
#include <boost/fiber/all.hpp>

#include "park_unpark.h"

class Socketbuf : public std::streambuf {
public:
    Socketbuf(int fd,ParkSupport& ps) : sockfd(fd), ps(ps) {
        setp(buffer, buffer + sizeof(buffer));
    }
    ~Socketbuf() { 
        close(sockfd); 
    }

protected:
    int underflow() override {
    again:
        int bytesRead = read(sockfd, buffer, sizeof(buffer));
        if (bytesRead <= 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                ps.park();
                goto again;
            }
            return std::char_traits<char>::eof();
        }
        setg(buffer, buffer, buffer + bytesRead);
        return *gptr();
    }

    int overflow(int c) override {
        if (c != EOF) {
            *pptr() = c;
            pbump(1);
        }
        return sync() == 0 ? c : EOF;
    }

    int sync() override {
        int len = pptr() - pbase();
    again:
        int sent = write(sockfd, buffer, len);
        if (sent < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                ps.park();
                goto again;
            }
            return -1;
        }
        pbump(-len);
        return 0;
    }

private:
    int sockfd;
    ParkSupport& ps;
    char buffer[4096];
};
