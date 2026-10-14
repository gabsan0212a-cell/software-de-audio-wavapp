#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Hilos para el cálculo pesado (separar voces). Reparte tareas numeradas entre los hilos: cada
// hilo toma la siguiente libre, así los núcleos rápidos hacen más y ninguno se queda esperando.
// Los hilos van con prioridad baja para que la pantalla y el audio sigan fluidos.
class ThreadPool {
public:
    // threads <= 0: uno menos que los núcleos (al menos 1), para dejar uno a la pantalla.
    explicit ThreadPool(int threads = 0);
    ~ThreadPool();
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    int size() const { return static_cast<int>(mThreads.size()); }
    // Llama fn(tarea, hilo) para tarea = 0..count-1 y espera a que terminen todas.
    // `hilo` va de 0 a size()-1 (para que cada hilo use su propia memoria de trabajo).
    // Si `stop` se prende, no empieza tareas nuevas. Si una tarea lanza una excepción, las demás
    // no empiezan y la excepción sale de aquí. Una llamada a la vez.
    void parallelFor(int count, const std::function<void(int, int)> &fn, const std::atomic<bool> *stop = nullptr);

private:
    void loop(int worker);

    std::vector<std::thread> mThreads;
    std::mutex mMutex;
    std::condition_variable mWake, mDone;
    const std::function<void(int, int)> *mFn = nullptr;
    const std::atomic<bool> *mStop = nullptr;
    int mCount = 0;
    std::atomic<int> mNext{0};
    int mBusy = 0;           // hilos trabajando en la ronda actual
    uint64_t mRound = 0;     // cambia con cada parallelFor
    bool mQuit = false;
    std::atomic<bool> mFailed{false};
    std::mutex mErrorMutex;
    std::exception_ptr mError;  // con mErrorMutex (o con la ronda terminada)
};
