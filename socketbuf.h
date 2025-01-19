#include <iostream>
#include <cstdio>
#include <unistd.h>

class Socketbuf : public std::streambuf {
public:
    Socketbuf(int fd) : sockfd(fd) {}
    ~Socketbuf() { 
        close(sockfd); 
    }

protected:
    int underflow() override {
        int bytesRead = read(sockfd, buffer, sizeof(buffer));
        if (bytesRead <= 0) {
            return std::char_traits<char>::eof();
        }
        setg(buffer, buffer, buffer + bytesRead);
        return *gptr();
    }

private:
    int sockfd;
    char buffer[4096];
};
