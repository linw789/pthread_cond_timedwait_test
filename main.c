#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#define ONE_MILLION 1000000
#define ONE_BILLION 1000000000

#define exit_if_error(en, msg) \
    do { \
        if (en != 0) { \
            errno = en; \
            perror(msg); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

typedef unsigned long u64;

int msleep(u64 msec) {
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

typedef struct {
    u64 start_ns;
    u64 during_ns;
} StopWatch;

int stopwatch_start(StopWatch* sw) {
    struct timespec curr_time = {};
    int err = clock_gettime(CLOCK_REALTIME, &curr_time);
    sw->start_ns = curr_time.tv_sec * ONE_BILLION + curr_time.tv_nsec;
    sw->during_ns = 0;
    return err;
}

int stopwatch_stop(StopWatch* sw) {
    struct timespec curr_time = {};
    int err = clock_gettime(CLOCK_REALTIME, &curr_time);
    u64 curr_ns = curr_time.tv_sec * ONE_BILLION + curr_time.tv_nsec;
    sw->during_ns = curr_ns - sw->start_ns;
    return err;
}

static int to_abstime(u64 relative_ms, struct timespec* out_abstime) {
    struct timespec curr_time = {};
    int err = clock_gettime(CLOCK_REALTIME, &curr_time);
    if (err == 0) {
        int overflow = 0;

        u64 relative_ns = 0;
        overflow |= __builtin_umull_overflow(relative_ms, (unsigned long)ONE_MILLION, &relative_ns);

        u64 end_nsec = 0;
        overflow |= __builtin_uaddl_overflow((unsigned long)curr_time.tv_nsec, relative_ns, &end_nsec);

        if (overflow != 0) {
            fprintf(stderr, "to_abstime overflow. relative_ms: %lu, curr_time.tv_sec: %ld, curr_time.tv_nsec: %ld\n",
                    relative_ms, curr_time.tv_sec, curr_time.tv_nsec);
        }

        u64 extra_sec = end_nsec / ONE_BILLION;
        out_abstime->tv_sec = curr_time.tv_sec + extra_sec;
        out_abstime->tv_nsec = end_nsec - extra_sec * ONE_BILLION; 
    } else {
        fprintf(stderr, "clock_gettime failed, error: %s.\n", strerror(err));
    }

    return err;
}

typedef struct {
    u64         sleep_ms;
    bool*            flag;
    pthread_mutex_t* mutex;
    pthread_cond_t*  cond;
} SetFlagThreadArgs;

static void* set_flag(void* userdata) {
    SetFlagThreadArgs* args = (SetFlagThreadArgs*)userdata;

    msleep(args->sleep_ms);

    int err = pthread_mutex_lock(args->mutex);
    if (err != 0) {
        fprintf(stderr, "set_flag:pthread_mutex_lock failed, error: %s.\n", strerror(err));
    }

    *args->flag = true;
    err = pthread_cond_signal(args->cond);
    if (err != 0) {
        fprintf(stderr, "set_flag:pthread_cond_singal failed, error: %s.\n", strerror(err));
    }

    err = pthread_mutex_unlock(args->mutex);
    if (err != 0) {
        fprintf(stderr, "set_flag:pthread_mutex_unlock failed, error: %s.\n", strerror(err));
    }

    return userdata;
}

int main(int argc, char** argv) {
    assert(sizeof(unsigned long) == sizeof(u64));

    u64 cond_wait_ms = (u64)atoi(argv[1]);
    if (cond_wait_ms == 0) {
        fprintf(stderr, "Failed parsing the first argument, or it's zero. argv[1]: %s.\n", argv[1]);
    }

    u64 set_flag_wait_ms = (u64)atoi(argv[2]);
    if (set_flag_wait_ms == 0) {
        fprintf(stderr, "Failed parsing the second argument, or it's zero. argv[2]: %s.\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    bool flag = false;

    pthread_mutex_t mutex;
    int err = pthread_mutex_init(&mutex, NULL);
    exit_if_error(err, "pthread_mutex_init");

    pthread_cond_t cond;
    err = pthread_cond_init(&cond, NULL);
    exit_if_error(err, "pthread_cond_init");

    // Create a thread to set flag.

    SetFlagThreadArgs sfa = {
        set_flag_wait_ms,
        &flag,
        &mutex,
        &cond
    };

    pthread_t set_flag_thread;
    err = pthread_create(&set_flag_thread, NULL, set_flag, &sfa);
    exit_if_error(err, "pthread_create");

    err = pthread_mutex_lock(&mutex);
    exit_if_error(err, "pthread_mutex_lock");

    struct timespec wait_time;
    err = to_abstime(cond_wait_ms, &wait_time);
    if (err != 0) {
        exit(EXIT_FAILURE);
    }

    flag = false;

    StopWatch stopwatch;

    int wait_result = 0;
    while (flag == false && wait_result == 0) {
        stopwatch_start(&stopwatch);

        wait_result = pthread_cond_timedwait(&cond, &mutex, &wait_time);

        stopwatch_stop(&stopwatch);
        printf("pthread_cond_timedwait returned after %lu ms. flag: %d, wait_result: %d\n", 
                stopwatch.during_ns / ONE_MILLION, flag, wait_result);
    }

    if (wait_result == 0) {
        printf("cond timed wait succeeded\n");
    } else {
        printf("cond timed wait failed, error: %s.\n", strerror(wait_result));
    }

    err = pthread_mutex_unlock(&mutex);
    exit_if_error(err, "pthread_mutex_unlock");

    void* set_flag_thread_res;
    err = pthread_join(set_flag_thread, &set_flag_thread_res);
    exit_if_error(err, "pthread_join");
    assert(set_flag_thread_res = &sfa);

    err = pthread_mutex_destroy(&mutex);
    exit_if_error(err, "pthread_mutex_destroy");

    err = pthread_cond_destroy(&cond);
    exit_if_error(err, "pthread_cond_destroy");

    exit(EXIT_SUCCESS);
}
