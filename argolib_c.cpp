
#include "argolib_c.h"

#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

using namespace std;

#include "sched_control.hpp"

#define DEFAULT_NUM_XSTREAMS 8

#ifdef __cplusplus
extern "C" {
#endif

ABT_pool *pools;
ABT_sched *scheds;
ABT_xstream *xstreams;
int use_optimization = 0;
unsigned long long num_ult;
int num_xstreams = DEFAULT_NUM_XSTREAMS;

typedef ABT_thread TaskHandle;
typedef void (*fork_t)(void *args);

void argolib_init(int argc, char **argv) {
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
    for (int i = 0; i < num_xstreams * 2; i++) {
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
    create_scheds(DEFAULT_NUM_XSTREAMS, pools, scheds, use_optimization);

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
    printf("\tElapsed Time: %f\n", end_time - start_time);
    printf("\tULTs Created: %lld\n", num_ult);
}

TaskHandle *argolib_fork(fork_t fptr, void *args) {
    int rank;
    TaskHandle thread_handle;
    ABT_xstream_self_rank(&rank);
    ABT_pool target_pool = pools[rank];

    num_ult++;

    ABT_thread_create(target_pool, fptr, args, ABT_THREAD_ATTR_NULL, &thread_handle);
    return (ABT_thread *)thread_handle;
}

void argolib_join(TaskHandle **handles, int size) {
    for (int i = 0; i < size; i++) {
        ABT_thread_join((ABT_thread)handles[i]);
        ABT_thread_free((ABT_thread *)&handles[i]);
    }
}

void argolib_finalize() {
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

void argolib_start_tracing() {
    if (tracing_enabled == false) {
        printf("[=] Tracing\n");
    }

    int idx = 0;
    for (auto worker_metadata : workers_metadata) {
        worker_metadata->steal_counter = 0;
        worker_metadata->async_counter = idx++ * (INT32_MAX / num_xstreams);
    }

    tracing_enabled = true;
}

void list_aggregation() {
    vector<vector<WorkerThreadStealInfo>> workers_steal_pll_new(num_xstreams);
    for (auto ll : workers_steal_pll) {
        for (auto tsi : ll) {
            workers_steal_pll_new[tsi.wc].push_back(tsi);
        }
    }

    workers_steal_pll = workers_steal_pll_new;
}

void list_sort() {
    for (auto v : workers_steal_pll) {
        sort(v.begin(), v.end());
    }
}

void argolib_stop_tracing() {
    if (replay_enabled == false) {
        list_aggregation();

        int idx = 0;
        for (auto ll : workers_steal_pll) {
            workers_steal_pll_ptr[idx++] = 0;
        }

        printf("[=] Replaying\n");

        list_sort();
    }

    replay_enabled = true;
}

#ifdef __cplusplus
}
#endif