#pragma once
#include <boost/fiber/all.hpp>

struct ParkSupport {
   private:
    boost::fibers::mutex mutex;
    boost::fibers::condition_variable cv;

    bool signaled = false;

   public:
    void park() {
        std::unique_lock<boost::fibers::mutex> lk(mutex);
        while (!signaled) {
            cv.wait(lk);
        }
        signaled = false;
    }

    void unpark() {
        std::lock_guard<boost::fibers::mutex> lock(mutex);
        signaled = true;
        cv.notify_one();
    }
};
