// Minimal libnx compatibility shim so the torrent ENGINE files (which include
// <switch.h>) compile and run on a normal POSIX PC, for fast debugging. Maps
// libnx threads/mutexes/timer/random onto pthreads + clock_gettime. This is a
// test-only stand-in — not a real libnx.
#ifndef PC_SWITCH_COMPAT_H
#define PC_SWITCH_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#ifdef _WIN32
#include <stdlib.h>
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef u32 Result;

//-----------------------------------------------------------------------------
// Mutex (atomic 3-state lazy init: 0=uninit, 1=initing, 2=ready)
//-----------------------------------------------------------------------------
typedef struct { pthread_mutex_t m; volatile int init; } Mutex;

static inline void mutexInit(Mutex *mtx) {
    pthread_mutex_init(&mtx->m, NULL);
    __atomic_store_n(&mtx->init, 2, __ATOMIC_RELEASE);
}
static inline void mutexEnsureInit(Mutex *mtx) {
    if (__atomic_load_n(&mtx->init, __ATOMIC_ACQUIRE) == 2) return;
    for (;;) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&mtx->init, &expected, 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            pthread_mutex_init(&mtx->m, NULL);
            __atomic_store_n(&mtx->init, 2, __ATOMIC_RELEASE);
            return;
        }
        if (__atomic_load_n(&mtx->init, __ATOMIC_ACQUIRE) == 2) return;
        sched_yield();
    }
}
static inline void mutexLock(Mutex *mtx) {
    mutexEnsureInit(mtx);
    pthread_mutex_lock(&mtx->m);
}
static inline void mutexUnlock(Mutex *mtx) { pthread_mutex_unlock(&mtx->m); }

//-----------------------------------------------------------------------------
// Condition variable
//-----------------------------------------------------------------------------
typedef struct { pthread_cond_t c; volatile int init; } CondVar;

static inline void condvarInit(CondVar *cv) {
    pthread_cond_init(&cv->c, NULL);
    __atomic_store_n(&cv->init, 2, __ATOMIC_RELEASE);
}
static inline void condvarEnsureInit(CondVar *cv) {
    if (__atomic_load_n(&cv->init, __ATOMIC_ACQUIRE) == 2) return;
    for (;;) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&cv->init, &expected, 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            pthread_cond_init(&cv->c, NULL);
            __atomic_store_n(&cv->init, 2, __ATOMIC_RELEASE);
            return;
        }
        if (__atomic_load_n(&cv->init, __ATOMIC_ACQUIRE) == 2) return;
        sched_yield();
    }
}
static inline void condvarWakeAll(CondVar *cv) {
    condvarEnsureInit(cv);
    pthread_cond_broadcast(&cv->c);
}
static inline Result condvarWaitTimeout(CondVar *cv, Mutex *mtx, u64 timeout_ns) {
    mutexEnsureInit(mtx);
    condvarEnsureInit(cv);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(timeout_ns / 1000000000ULL);
    ts.tv_nsec += (long)(timeout_ns % 1000000000ULL);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return (Result)pthread_cond_timedwait(&cv->c, &mtx->m, &ts);
}

//-----------------------------------------------------------------------------
// Thread (libnx splits create/start; we defer pthread_create to threadStart)
//-----------------------------------------------------------------------------
typedef void (*ThreadFunc)(void *);
typedef struct {
    pthread_t pt;
    ThreadFunc entry;
    void *arg;
    u32 handle;   // engine treats handle==0 as "not created"
    int started;
} Thread;

static inline Result threadCreate(Thread *t, ThreadFunc entry, void *arg,
                                  void *stack, size_t stack_sz, int prio, int cpu) {
    (void)stack; (void)stack_sz; (void)prio; (void)cpu;
    t->entry = entry;
    t->arg = arg;
    t->handle = 1;
    t->started = 0;
    return 0;
}
static inline void *pc_thread_trampoline(void *p) {
    Thread *t = (Thread *)p;
    t->entry(t->arg);
    return NULL;
}
static inline Result threadStart(Thread *t) {
    int rc = pthread_create(&t->pt, NULL, pc_thread_trampoline, t);
    t->started = (rc == 0) ? 1 : 0;
    return (Result)rc;
}
static inline Result threadWaitForExit(Thread *t) {
    if (t->started) {
        pthread_join(t->pt, NULL);
        t->started = 0;
    }
    return 0;
}
static inline Result threadClose(Thread *t) { t->handle = 0; t->started = 0; return 0; }

//-----------------------------------------------------------------------------
// Timing: use CLOCK_MONOTONIC nanoseconds as "ticks" with a 1e9 frequency.
//-----------------------------------------------------------------------------
static inline u64 armGetSystemTick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
}
static inline u64 armGetSystemTickFreq(void) { return 1000000000ULL; }
static inline u64 armNsToTicks(u64 ns) { return ns; }
static inline u64 armTicksToNs(u64 t) { return t; }

static inline void svcSleepThread(u64 ns) {
    struct timespec ts = { (time_t)(ns / 1000000000ULL),
                           (long)(ns % 1000000000ULL) };
    nanosleep(&ts, NULL);
}

// No real core affinity on the PC; the engine only uses this for its debug
// heartbeat, so a constant is fine.
static inline u32 svcGetCurrentProcessorNumber(void) { return 0; }

//-----------------------------------------------------------------------------
// Random
//-----------------------------------------------------------------------------
static inline void randomGet(void *buf, size_t len) {
#if defined(__ANDROID__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(buf, len);
#else
    static pthread_mutex_t rng_mtx = PTHREAD_MUTEX_INITIALIZER;
    static u64 state = 0;
    pthread_mutex_lock(&rng_mtx);
    if (state == 0) {
        state = armGetSystemTick() ^ (u64)(uintptr_t)&rng_mtx ^ 0x9e3779b97f4a7c15ULL;
        if (state == 0) state = 1;
    }
    u8 *b = (u8 *)buf;
    for (size_t i = 0; i < len; i++) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        u64 v = state * 0x2545F4914F6CDD1DULL;
        b[i] = (u8)(v >> 56);
    }
    pthread_mutex_unlock(&rng_mtx);
#endif
}

#endif
