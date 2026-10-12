#include "Worker.h"

#include <chrono>

Worker::~Worker() {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mQuit = true;
        mJobs.clear();
    }
    mWake.notify_all();
    if (mThread.joinable()) mThread.join();
}

void Worker::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        if (mQuit) return;
        mJobs.push_back(std::move(job));
        if (!mThread.joinable()) mThread = std::thread([this] { loop(); });
    }
    mWake.notify_one();
}

void Worker::loop() {
    std::unique_lock<std::mutex> lk(mMutex);
    while (true) {
        mWake.wait(lk, [this] { return mQuit || !mJobs.empty(); });
        if (mQuit) break;
        std::function<void()> job = std::move(mJobs.front());
        mJobs.pop_front();
        mWorking = true;
        lk.unlock();
        try {
            job();
        } catch (...) {
            // Un trabajo que falla no debe tumbar el hilo (los demás siguen).
        }
        lk.lock();
        mWorking = false;
        if (mJobs.empty()) mIdle.notify_all();
    }
    mWorking = false;
    mIdle.notify_all();
}

bool Worker::idle() {
    std::lock_guard<std::mutex> lk(mMutex);
    return !mWorking && mJobs.empty();
}

bool Worker::waitIdle(int timeoutMs) {
    std::unique_lock<std::mutex> lk(mMutex);
    return mIdle.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] { return !mWorking && mJobs.empty(); });
}
