#ifndef DOS_STUBS_H
#define DOS_STUBS_H

#include <stddef.h>
#include <malloc.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#ifndef ENOBUFS
#define ENOBUFS 105
#endif

#ifndef ENODATA
#define ENODATA 61
#endif

#ifndef ENOMSG
#define ENOMSG 42
#endif

#ifndef ENOTSUP
#define ENOTSUP 134
#endif

#ifndef EBADMSG
#define EBADMSG 74
#endif

#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 1000
#endif

#define DOS_CORE 1

typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_cond_t;

static inline int pthread_mutex_init(pthread_mutex_t *mutex, void *attr) { return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) { return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *mutex) { return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) { return 0; }

static inline int pthread_cond_init(pthread_cond_t *cond, void *attr) { return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *cond) { return 0; }
static inline int pthread_cond_signal(pthread_cond_t *cond) { return 0; }
static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) { return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *cond) { return 0; }

static inline int pthread_create(pthread_t *thread, void *attr, void *(*start_routine) (void *), void *arg) { return -1; }
static inline int pthread_cancel(pthread_t thread) { return 0; }

static inline long sysconf(int name) { return 1; }

#define CLOCK_PROCESS_CPUTIME_ID 2
static inline int clock_gettime(int clk_id, struct timespec *tp) { tp->tv_sec = 0; tp->tv_nsec = 0; return 0; }

static inline void *my_aligned_alloc(size_t align, size_t size) {
    if (align < sizeof(void*)) align = sizeof(void*);
    void *ptr = __builtin_malloc(size + align + sizeof(void*));
    if (!ptr) return NULL;
    void *aligned = (void*)(((uintptr_t)ptr + sizeof(void*) + align - 1) & ~(align - 1));
    ((void**)aligned)[-1] = ptr;
    return aligned;
}

static inline void my_aligned_free(void *ptr) {
    if (ptr) {
        __builtin_free(((void**)ptr)[-1]);
    }
}

#define aligned_alloc(align, size) my_aligned_alloc(align, size)
#define malloc(size) my_aligned_alloc(16, size)
#define free(ptr) my_aligned_free(ptr)

#endif
