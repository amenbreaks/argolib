#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "./pmu/pcm.h"
#include "argolib.hpp"
#include "sched_control.hpp"

#define DEFAULT_NUM_XSTREAMS 8
#define INC 1
#define DEC 0

ABT_pool *pools;
ABT_sched *scheds;
ABT_xstream *xstreams;
int use_optimization = 0;
atomic<unsigned long long> num_ult;
int num_xstreams = DEFAULT_NUM_XSTREAMS;
int shutdown = 1;
int conf_DOP_cfft = 1;

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

    /* Starting Performance Counter Monitor */
    shutdown = 0;
    logger::start();

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
        WorkerMetadata *wm = (WorkerMetadata *)malloc(sizeof(WorkerMetadata *));
        wm->steal_counter = 0;
        wm->async_counter = idx * UINT64_MAX / num_xstreams;

        wm->should_sleep = false;
        pthread_cond_init(&wm->p_sleep_cond, NULL);
        pthread_mutex_init(&wm->p_sleep_mutex, NULL);

        workers_metadata.push_back(wm);
        workers_steal_pll_ptr.push_back(0);
        workers_steal_pll.push_back(vector<WorkerThreadStealInfo>(0));
    }

    ABT_key_create(NULL, &key_thread_id);
}  // namespace argolib

void __executor(void *arg) { ((ThreadHandleArgs *)arg)->lambda(); }

template <typename T>
void kernel(T &&lambda) {
    num_ult = 0;
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
                    ABT_pool_push_thread(pools[thread_info.we], thread_handle.thread);
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
    /* Shutting Down PCM */
    shutdown = 1;
    logger::___pcm->cleanup();

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

void stop_tracing() {
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

void sleep_argolib_num_workers(int n) {
    int put_to_sleep = 0;

    for (auto wm : workers_metadata) {
        if (put_to_sleep == n) {
            break;
        }

        if (wm->p_sleep == false) {
            wm->should_sleep = true;
            put_to_sleep++;
        }
    }
}

void awake_argolib_num_workers(int n) {
    int awake_from_sleep = 0;

    for (auto wm : workers_metadata) {
        if (awake_from_sleep == n) {
            break;
        }

        if (wm->p_sleep == true) {
            pthread_cond_signal(wm->p_sleep_cond);
            awake_from_sleep++;
        }
    }
}

void configure_DOP(double JPI_prev, double JPI_curr) {
    static int DP_last_action = INC, wActive = num_xstreams;
    const int wChange = 2;  // find experimentally on your system
    if (conf_DOP_cfft) {
        sleep_argolib_num_workers(wChange);
        DP_last_action = DEC;
        conf_DOP_cfft = 0;
        return;
    }
    if (JPI_prev > JPI_curr) {
        if (DP_last_action == DEC) {
            sleep_argolib_num_workers(wChange);
        } else {
            awake_argolib_num_workers(wChange);
        }
    } else {
        if (DP_last_action == DEC) {
            awake_argolib_num_workers(wChange);
            DP_last_action = INC;
        } else {
            sleep_argolib_num_workers(wChange);
            DP_last_action = DEC;
        }
    }
}

void daemon_profiler() {           // a dedicated pthread
    const int fixed_interval = 2;  // some value that you find experimentally
    sleep(1000);                   // warmup duration
    double JPI_prev = 0;           // JPI is Joules per Instructions Retired
    while (!shutdown) {
        double JPI_curr = logger::end();
        logger::___before_sstate = pcm::getSystemCounterState();
        configure_DOP(JPI_prev, JPI_curr);
        JPI_prev = JPI_curr;
        sleep(fixed_interval);
    }
}

}  // namespace argolib
