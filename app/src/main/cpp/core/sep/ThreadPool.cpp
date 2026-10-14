#include "sep/ThreadPool.h"

#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>

ThreadPool::ThreadPool(int threads) {
    if (threads <= 0) {
        const int cores = static_cast<int>(std::thread::hardware_concurrency());
        threads = std::max(1, cores - 1);
    }
    mThreads.reserve(static_cast<size_t>(threads));
    for (int i = 0; i < threads; ++i) mThreads.emplace_back([this, i] { loop(i); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mQuit = true;
    }
    mWake.notify_all();
    for (auto &t : mThreads) t.join();
}

void ThreadPool::parallelFor(int count, const std::function<void(int, int)> &fn, const std::atomic<bool> *stop) {
    if (count <= 0) return;
    std::unique_lock<std::mutex> lk(mMutex);
    mFn = &fn;
    mStop = stop;
    mCount = count;
    mNext.store(0);
    mFailed.store(false);
    mBusy = static_cast<int>(mThreads.size());
    ++mRound;
    mWake.notify_all();
    mDone.wait(lk, [this] { return mBusy == 0; });
    mFn = nullptr;
    mStop = nullptr;
    // Si una tarea falló (p. ej. sin memoria), el error sale aquí, en quien pidió el trabajo.
    if (mError) {
        std::exception_ptr e = mError;
        mError = nullptr;
        std::rethrow_exception(e);
    }
}

void ThreadPool::loop(int worker) {
    // Prioridad baja: el cálculo largo no le quita fluidez a la pantalla ni al audio.
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
    uint64_t seen = 0;
    std::unique_lock<std::mutex> lk(mMutex);
    for (;;) {
        mWake.wait(lk, [&] { return mQuit || mRound != seen; });
        if (mQuit) return;
        seen = mRound;
        const std::function<void(int, int)> *fn = mFn;
        const std::atomic<bool> *stop = mStop;
        const int count = mCount;
        lk.unlock();
        for (;;) {
            if (stop && stop->load(std::memory_order_relaxed)) break;
            if (mFailed.load(std::memory_order_relaxed)) break;
            const int i = mNext.fetch_add(1);
            if (i >= count) break;
            try {
                (*fn)(i, worker);
            } catch (...) {
                std::lock_guard<std::mutex> e(mErrorMutex);
                if (!mError) mError = std::current_exception();
                mFailed.store(true);
            }
        }
        lk.lock();
        if (--mBusy == 0) mDone.notify_all();
    }
}
