#include <stdlib.h>
#include <unistd.h>

#include <functional>

#include "abt.h"
#include "sched_control.hpp"

#define DEFAULT_NUM_XSTREAMS 8

ABT_pool *pools;
ABT_sched *scheds;
ABT_xstream *xstreams;
int num_xstreams = DEFAULT_NUM_XSTREAMS;

namespace argolib {
typedef struct {
    std::function<void()> lambda;
} ThreadHandleArgs;

typedef struct {
    ABT_thread thread;
    ThreadHandleArgs args;
} TaskHandle;

void init(int argc, char **argv) {
    if (char *_env = getenv("ARGOLIB_WORKERS")) {
        if (int _num_xstreams = atoi(_env)) {
            num_xstreams = _num_xstreams;
        }
    }

    /* Allocate memory. */
    pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams * 2);
    scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * num_xstreams);
    xstreams = (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);

    /* Initialize Argobots. */
    ABT_init(argc, argv);

    /* Create pools. */
    for (int i = 0; i < num_xstreams * 2; i++) {
        if (i >= num_xstreams) {
            // The last N pools are private to one pool
            ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_PRIV, ABT_TRUE, &pools[i]);
        } else {
            // First N Pools will be public
            ABT_pool_create_basic(ABT_POOL_RANDWS, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &pools[i]);
        }
    }

    // Set the random seed
    srand(time(NULL));

    /* Create schedulers. */
    sched_control::create_scheds(DEFAULT_NUM_XSTREAMS, pools, scheds);

    /* Set up a primary execution stream. */
    ABT_xstream_self(&xstreams[0]);
    ABT_xstream_set_main_sched(xstreams[0], scheds[0]);

    /* Create secondary execution streams. */
    for (int i = 1; i < num_xstreams; i++) {
        ABT_xstream_create(scheds[i], &xstreams[i]);
    }
}

template <typename T>
void kernel(T &&lambda) {
    double start_time = ABT_get_wtime();
    lambda();
    double end_time = ABT_get_wtime();
    printf("[*] Kernel Report\n");
    printf("\tElapsed Time:%f\n", end_time - start_time);
}

void __executor(void *arg) { ((ThreadHandleArgs *)arg)->lambda(); }

template <typename T>
TaskHandle fork(T &&lambda) {
    int rank;
    TaskHandle thread_handle;
    ABT_xstream_self_rank(&rank);
    ABT_pool target_pool = pools[rank];

    thread_handle.args.lambda = lambda;

    ABT_thread_create(target_pool, (void (*)(void *)) & __executor, (void *)&thread_handle.args, ABT_THREAD_ATTR_NULL,
                      &thread_handle.thread);

    return thread_handle;
}

template <typename... TaskHandle>
void join(TaskHandle... handles) {
    ((ABT_thread_join(handles.thread)), ...);
}

void finalize() {
    /* Join secondary execution streams. */
    for (int i = 1; i < num_xstreams; i++) {
        ABT_xstream_join(xstreams[i]);
        ABT_xstream_free(&xstreams[i]);
    }

    /* Finalize Argobots. */
    ABT_finalize();

    /* Free allocated memory. */
    free(xstreams);
    free(pools);

    for (int i = 1; i < DEFAULT_NUM_XSTREAMS; i++) {
        ABT_sched_free(&scheds[i]);
    }
    free(scheds);
}

}  // namespace argolib
