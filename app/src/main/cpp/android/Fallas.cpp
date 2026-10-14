#include "Fallas.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>

// Dentro de una señal casi nada es seguro: aquí solo se escribe con write() a un archivo cuyo
// nombre ya quedó listo, y la pila se recorre con el desenrollador (como hacen los reportes de
// fallas de Android).

namespace {

char gPath[512];
constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
struct sigaction gOld[sizeof(kSignals) / sizeof(kSignals[0])];
std::terminate_handler gOldTerminate = nullptr;
volatile sig_atomic_t gInside = 0;      // ya se está anotando una falla
volatile sig_atomic_t gTerminated = 0;  // la excepción ya se anotó (luego llega SIGABRT)

void put(int fd, const char *s) {
    if (s) (void)!write(fd, s, std::strlen(s));
}

void putHex(int fd, uintptr_t v) {
    char b[2 + 2 * sizeof(uintptr_t) + 1];
    int i = static_cast<int>(sizeof(b)) - 1;
    b[i] = '\0';
    do {
        b[--i] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v && i > 2);
    b[--i] = 'x';
    b[--i] = '0';
    put(fd, b + i);
}

void putDec(int fd, long v) {
    char b[24];
    int i = static_cast<int>(sizeof(b)) - 1;
    b[i] = '\0';
    const bool neg = v < 0;
    unsigned long u = neg ? static_cast<unsigned long>(-v) : static_cast<unsigned long>(v);
    do {
        b[--i] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u && i > 1);
    if (neg) b[--i] = '-';
    put(fd, b + i);
}

const char *nombreSenal(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (acceso a memoria inválido)";
        case SIGBUS: return "SIGBUS (acceso a memoria mal alineado)";
        case SIGFPE: return "SIGFPE (cuenta inválida, p. ej. división entera entre cero)";
        case SIGILL: return "SIGILL (instrucción inválida)";
        case SIGABRT: return "SIGABRT (el programa se detuvo a sí mismo)";
        default: return "señal";
    }
}

// Una línea por dirección: biblioteca + desplazamiento (y el símbolo, si es público).
void linea(int fd, int n, uintptr_t pc) {
    put(fd, "  #");
    if (n < 10) put(fd, "0");
    putDec(fd, n);
    put(fd, " ");
    Dl_info info{};
    if (pc && dladdr(reinterpret_cast<void *>(pc), &info) && info.dli_fname) {
        const char *base = std::strrchr(info.dli_fname, '/');
        put(fd, base ? base + 1 : info.dli_fname);
        put(fd, " +");
        putHex(fd, pc - reinterpret_cast<uintptr_t>(info.dli_fbase));
        if (info.dli_sname) {
            put(fd, " (");
            put(fd, info.dli_sname);
            put(fd, ")");
        }
    } else {
        putHex(fd, pc);
    }
    put(fd, "\n");
}

struct Pila {
    uintptr_t pcs[40];
    int n = 0;
};

_Unwind_Reason_Code paso(_Unwind_Context *ctx, void *arg) {
    Pila *p = static_cast<Pila *>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && p->n < 40) p->pcs[p->n++] = pc;
    return p->n < 40 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void escribirPila(int fd) {
    Pila p;
    _Unwind_Backtrace(paso, &p);
    put(fd, "Pila:\n");
    for (int i = 0; i < p.n; ++i) linea(fd, i, p.pcs[i]);
}

void lugarDeLaFalla(int fd, void *ucv) {
    if (!ucv) return;
    const ucontext_t *uc = static_cast<const ucontext_t *>(ucv);
    uintptr_t pc = 0, lr = 0;
#if defined(__aarch64__)
    pc = uc->uc_mcontext.pc;
    lr = uc->uc_mcontext.regs[30];
#elif defined(__arm__)
    pc = uc->uc_mcontext.arm_pc;
    lr = uc->uc_mcontext.arm_lr;
#elif defined(__x86_64__)
    pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__)
    pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_EIP]);
#endif
    put(fd, "Donde falló:\n");
    linea(fd, 0, pc);
    if (lr) linea(fd, 1, lr);
}

void alSenal(int sig, siginfo_t *si, void *uc) {
    int idx = -1;
    for (int i = 0; i < static_cast<int>(sizeof(kSignals) / sizeof(kSignals[0])); ++i)
        if (kSignals[i] == sig) idx = i;
    if (!gInside) {
        gInside = 1;
        const int fd = open(gPath, O_WRONLY | O_CREAT | (gTerminated ? O_APPEND : O_TRUNC), 0644);
        if (fd >= 0) {
            put(fd, gTerminated ? "\n" : "El núcleo (C++) se cerró por un error.\n");
            put(fd, "Señal: ");
            put(fd, nombreSenal(sig));
            if (si && sig != SIGABRT) {
                put(fd, " en ");
                putHex(fd, reinterpret_cast<uintptr_t>(si->si_addr));
            }
            put(fd, "\nHilo: ");
            putDec(fd, static_cast<long>(gettid()));
            put(fd, "\n");
            lugarDeLaFalla(fd, uc);
            escribirPila(fd);
            close(fd);
        }
    }
    // Lo que había antes (el reporte del sistema): con la falla tal cual.
    if (idx >= 0) {
        struct sigaction &old = gOld[idx];
        sigaction(sig, &old, nullptr);
        if ((old.sa_flags & SA_SIGINFO) && old.sa_sigaction) {
            old.sa_sigaction(sig, si, uc);
            return;
        }
        if (!(old.sa_flags & SA_SIGINFO) && old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
            old.sa_handler(sig);
            return;
        }
    }
    // Con la acción de siempre: una falla de memoria vuelve a pasar al regresar y cierra la app;
    // abort() vuelve a mandar SIGABRT.
}

void alTerminar() {
    if (!gInside) {
        const int fd = open(gPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            put(fd, "El núcleo (C++) se cerró por una excepción que nadie atrapó");
            if (std::exception_ptr e = std::current_exception()) {
                try {
                    std::rethrow_exception(e);
                } catch (const std::exception &x) {
                    put(fd, ": ");
                    put(fd, x.what());
                } catch (...) {
                    put(fd, " (de un tipo desconocido)");
                }
            }
            put(fd, "\n");
            escribirPila(fd);
            close(fd);
            gTerminated = 1;
        }
    }
    if (gOldTerminate) gOldTerminate();
    std::abort();
}

}  // namespace

void instalarFallas(const std::string &path) {
    static bool installed = false;
    std::strncpy(gPath, path.c_str(), sizeof(gPath) - 1);
    gPath[sizeof(gPath) - 1] = '\0';
    if (installed) return;
    installed = true;
    for (int i = 0; i < static_cast<int>(sizeof(kSignals) / sizeof(kSignals[0])); ++i) {
        struct sigaction sa {};
        sa.sa_sigaction = alSenal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(kSignals[i], &sa, &gOld[i]);
    }
    gOldTerminate = std::set_terminate(alTerminar);
}
