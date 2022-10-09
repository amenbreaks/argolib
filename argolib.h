#include <stdlib.h>
#include <unistd.h>

#include "abt.h"
#include "sched_control.h"

#define DEFAULT_NUM_XSTREAMS 8

ABT_pool *pools;
ABT_sched *scheds;
ABT_xstream *xstreams;
int num_xstreams = DEFAULT_NUM_XSTREAMS;

typedef ABT_thread TaskHandle;
typedef void (*fork_t)(void *args);

void argolib_init(int argc, char **argv) {
    char *_env = getenv("ARGOLIB_WORKERS");
    if (_env != NULL) {
        int _num_xstreams = atoi(_env);
        if (_num_xstreams > 0) {
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
    create_scheds(DEFAULT_NUM_XSTREAMS, pools, scheds);

    /* Set up a primary execution stream. */
    ABT_xstream_self(&xstreams[0]);
    ABT_xstream_set_main_sched(xstreams[0], scheds[0]);

    /* Create secondary execution streams. */
    for (int i = 1; i < num_xstreams; i++) {
        ABT_xstream_create(scheds[i], &xstreams[i]);
    }
}

void argolib_kernel(fork_t fptr, void *args) {
    double start_time = ABT_get_wtime();
    fptr(args);
    double end_time = ABT_get_wtime();
    printf("[*] Kernel Report\n");
    printf("\tElapsed Time:%f\n", end_time - start_time);
}

TaskHandle *argolib_fork(fork_t fptr, void *args) {
    int rank;
    TaskHandle thread_handle;
    ABT_xstream_self_rank(&rank);
    ABT_pool target_pool = pools[rank];

    ABT_thread_create(target_pool, fptr, args, ABT_THREAD_ATTR_NULL, &thread_handle);
    return thread_handle;
}

void argolib_join(TaskHandle **handles, int size) {
    for (int i = 0; i < size; i++) {
        ABT_thread_join(handles[i]);
    }
}

void argolib_finalize() {
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
