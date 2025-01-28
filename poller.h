#include <sys/event.h>
#include <unistd.h>
#include <cerrno>
#include <vector>
#include <functional>

struct Poller {
    int kqueue_fd;
    std::vector<struct kevent> events;

    static const uint32_t events_mask = EVFILT_READ | EVFILT_WRITE | EV_ERROR;

    std::atomic<bool> running = true;

    Poller(int max_events = 10) : events(max_events) {
        kqueue_fd = kqueue();
        if (kqueue_fd == -1) {
            throw std::runtime_error("Failed to create kqueue file descriptor");
        }
    }

    ~Poller() {
        ::close(kqueue_fd);
    }

    void add_socket(int socket_fd, void* user_data, std::function<void(struct kevent&, void*)> callback) {
        struct kevent event;
        auto* cb_data = new std::pair<std::function<void(struct kevent&, void*)>, void*>(callback, user_data);
        EV_SET(&event, socket_fd, events_mask, EV_ADD, 0, 0, reinterpret_cast<void*>(cb_data));
        if (kevent(kqueue_fd, &event, 1, nullptr, 0, nullptr) == -1) {
            delete cb_data;
            throw std::runtime_error("Failed to add socket to kqueue");
        }
    }

    void remove_socket(int socket_fd) {
        struct kevent event;
        EV_SET(&event, socket_fd, events_mask, EV_DELETE, 0, 0, nullptr);
        if (kevent(kqueue_fd, &event, 1, nullptr, 0, nullptr) == -1) {
            if(errno==EBADF || errno==ENOENT) return;
            throw std::runtime_error("failed to remove socket from kqueue");
        }
    }

    void poll() {
        int num_events = kevent(kqueue_fd, nullptr, 0, events.data(), events.size(), nullptr);
        if (num_events == -1) {
            throw std::runtime_error("error during kqueue wait");
        }
        for (int i = 0; i < num_events; i++) {
            auto* cb_data = reinterpret_cast<std::pair<std::function<void(struct kevent&, void*)>, void*>*>(events[i].udata);
            cb_data->first(events[i], cb_data->second);
        }
    }
    void close() {
        ::close(kqueue_fd);
    }
};
