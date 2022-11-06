#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "argolib.hpp"
#include "sched_control.hpp"

#define DEFAULT_NUM_XSTREAMS 3

ABT_pool *pools;
ABT_sched *scheds;
ABT_xstream *xstreams;
int use_optimization = 0;
atomic<unsigned long long> num_ult;
int num_xstreams = DEFAULT_NUM_XSTREAMS;

using namespace std;

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
    create_scheds(num_xstreams, pools, scheds, use_optimization);

    /* Set up a primary execution stream. */
    ABT_xstream_self(&xstreams[0]);
    ABT_xstream_set_main_sched(xstreams[0], scheds[0]);

    /* Create secondary execution streams. */
    for (int i = 1; i < num_xstreams; i++) {
        ABT_xstream_create(scheds[i], &xstreams[i]);
    }

    for (int idx = 0; idx < num_xstreams; idx++) {
        WorkerMetadata *wm = (WorkerMetadata *)malloc(sizeof(wm));
        wm->steal_counter = 0;
        wm->async_counter = idx * UINT64_MAX / num_xstreams;

        workers_metadata.push_back(wm);
        workers_steal_pll_ptr.push_back(0);
        workers_steal_pll.push_back(vector<WorkerThreadStealInfo>(0));
    }

    ABT_key_create(NULL, &key_thread_id);
}  // namespace argolib

void __executor(void *arg) { ((ThreadHandleArgs *)arg)->lambda(); }

template <typename T>
void kernel(T &&lambda) {
    double start_time = ABT_get_wtime();

    TaskHandle thread_handle;
    thread_handle.args.lambda = lambda;
    ABT_thread_create(pools[0], (void (*)(void *)) & __executor, (void *)&thread_handle.args, ABT_THREAD_ATTR_NULL,
                      &thread_handle.thread);
    ABT_thread_join(thread_handle.thread);

    double end_time = ABT_get_wtime();
    printf("[*] Kernel Report\n");
    printf("\tElapsed Time: %f\n", end_time - start_time);
    printf("\tULTs Created: %lld\n", num_ult.load());
}

template <typename T>
TaskHandle fork(T &&lambda) {
    int rank;
    TaskHandle thread_handle;
    ABT_xstream_self_rank(&rank);
    ABT_pool target_pool = pools[rank];
    ABT_pool target_pool_priv = pools[rank + num_xstreams];

    num_ult++;
    thread_handle.args.lambda = lambda;

    if (tracing_enabled || replay_enabled) {
        ABT_thread_create(target_pool_priv, (void (*)(void *)) & __executor, (void *)&thread_handle.args,
                          ABT_THREAD_ATTR_NULL, &thread_handle.thread);
        ABT_pool_pop_thread(target_pool_priv, &thread_handle.thread);

        uint64_t tid = ++workers_metadata[rank]->async_counter;
        ABT_thread_set_specific(thread_handle.thread, (ABT_key)key_thread_id, (void *)tid);

        if (replay_enabled) {
            int cur = workers_steal_pll_ptr[rank];
            if (workers_steal_pll[rank].size() > cur) {
                auto thread_info = workers_steal_pll[rank][cur];

                if (thread_info.tid == tid) {
                    workers_steal_arr[thread_info.we][thread_info.sc] = thread_handle.thread;
                    workers_steal_pll_ptr[rank]++;
                } else {
                    ABT_pool_push_thread(target_pool, thread_handle.thread);
                }
            } else {
                ABT_pool_push_thread(target_pool, thread_handle.thread);
            }
        } else {
            if (thread_handle.thread != ABT_THREAD_NULL) {
                ABT_pool_push_thread(target_pool, thread_handle.thread);
            }
        }
    } else {
        ABT_thread_create(target_pool, (void (*)(void *)) & __executor, (void *)&thread_handle.args,
                          ABT_THREAD_ATTR_NULL, &thread_handle.thread);
    }

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

void start_tracing() {
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

void stop_tracing() {
    if (replay_enabled == false) {
        list_aggregation();

        for (int i = 0; i < num_xstreams; i++) {
            workers_steal_arr.push_back(vector<ABT_thread>(0));
        }

        int idx = 0;
        for (auto ll : workers_steal_pll) {
            for (auto tsi : ll) {
                workers_steal_arr[tsi.we].push_back(ABT_THREAD_NULL);
            }

            workers_steal_pll_ptr[idx++] = 0;
        }

        printf("[=] Replaying\n");

        list_sort();
    }

    replay_enabled = true;
}

}  // namespace argolib
