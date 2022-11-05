#include <stdlib.h>
#include <unistd.h>

#include "argolib.hpp"

#define DEFAULT_NUM_XSTREAMS 8

ABT_pool *pools;
ABT_sched *scheds;
ABT_xstream *xstreams;
int use_optimization = 0;
unsigned long long num_ult;
int num_xstreams = DEFAULT_NUM_XSTREAMS;

namespace argolib {
void init(int argc, char **argv) {
    char *_num_workers_str = getenv("ARGOLIB_WORKERS");
    if (_num_workers_str) {
        int _num_xstreams = atoi(_num_workers_str);
        if (_num_xstreams > 0) {
            num_xstreams = _num_xstreams;
        }
    }

    char *_optimizations_str = getenv("ARGOLIB_OPTIMIZATION");
    if (_optimizations_str) {
        use_optimization = atoi(_optimizations_str);
        if (use_optimization) {
            printf("[+] Argolib: Using optimization\n");
        }
    }

    /* Allocate memory. */
    pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams * 2);
    scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * num_xstreams);
    xstreams = (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);

    /* Initialize Argobots. */
    ABT_init(argc, argv);

    /* Create pools. */
    for (int i = 0; i < num_xstreams; i++) {
        ABT_pool_access access;

        if (use_optimization) {
            // Create private pools
            access = ABT_POOL_ACCESS_PRIV;
        } else {
            // Create public pools
            access = ABT_POOL_ACCESS_MPSC;
        }

        ABT_pool_create_basic(ABT_POOL_RANDWS, access, ABT_TRUE, &pools[i]);
    }

    /* Create schedulers. */
    create_scheds(num_xstreams, pools, scheds, use_optimization);

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
    printf("\tElapsed Time: %f\n", end_time - start_time);
    printf("\tULTs Created: %lld\n", num_ult);
}

void __executor(void *arg) { ((ThreadHandleArgs *)arg)->lambda(); }

template <typename T>
TaskHandle fork(T &&lambda) {
    int rank;
    TaskHandle thread_handle;
    ABT_xstream_self_rank(&rank);
    ABT_pool target_pool = pools[rank];

    num_ult++;
    thread_handle.args.lambda = lambda;

    ABT_thread_create(target_pool, (void (*)(void *)) & __executor, (void *)&thread_handle.args, ABT_THREAD_ATTR_NULL,
                      &thread_handle.thread);

    return thread_handle;
}

template <typename... TaskHandle>
void join(TaskHandle... handles) {
    ((ABT_thread_join(handles.thread)), ...);
    ((ABT_thread_free(&handles.thread)), ...);
}

void finalize() {
    /* Join secondary execution streams. */
    for (int i = 1; i < num_xstreams; i++) {
        ABT_sched_exit(scheds[i]);
        ABT_xstream_join(xstreams[i]);

        ABT_xstream_free(&xstreams[i]);
        ABT_sched_free(&scheds[i]);
    }

    /* Finalize Argobots. */
    ABT_finalize();

    /* Free allocated memory. */
    free(xstreams);
    free(scheds);
    free(pools);
}

}  // namespace argolib
