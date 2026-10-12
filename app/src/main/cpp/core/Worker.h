#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

// Un hilo que hace trabajos largos, uno tras otro (importar, exportar, abrir la sesión...),
// para que ni la pantalla ni el audio esperen. El hilo nace con el primer trabajo.
class Worker {
public:
    Worker() = default;
    ~Worker();  // termina el trabajo en curso; los pendientes se descartan
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;

    void post(std::function<void()> job);
    // Espera a que no quede nada por hacer. false si pasó el tiempo (pruebas).
    bool waitIdle(int timeoutMs);
    bool idle();

private:
    void loop();

    std::mutex mMutex;
    std::condition_variable mWake, mIdle;
    std::deque<std::function<void()>> mJobs;
    bool mWorking = false;
    bool mQuit = false;
    std::thread mThread;
};
