#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "abt.h"

#define PUBLIC_POOL_THRESHOLD 4

namespace sched_control {
typedef struct {
    uint32_t event_freq;
} sched_data_t;

static int sched_init(ABT_sched sched, ABT_sched_config config) {
    sched_data_t *p_data = (sched_data_t *)calloc(1, sizeof(sched_data_t));

    ABT_sched_config_read(config, 1, &p_data->event_freq);
    ABT_sched_set_data(sched, (void *)p_data);

    return ABT_SUCCESS;
}

static void sched_run(ABT_sched sched) {
    int num_pools;
    ABT_pool *pools;
    uint32_t work_count = 0;
    sched_data_t *sched_data;

    ABT_sched_get_num_pools(sched, &num_pools);
    ABT_sched_get_data(sched, (void **)&sched_data);

    pools = (ABT_pool *)malloc(num_pools * sizeof(ABT_pool));
    ABT_sched_get_pools(sched, num_pools, 0, pools);

    while (1) {
        ABT_thread thread = ABT_THREAD_NULL;

        // Try popping from self private pool
        ABT_pool_pop_thread(pools[0], &thread);
        if (thread == ABT_THREAD_NULL && num_pools >= 1) {
            // If private pool was empty, try popping from self public pool
            ABT_pool_pop_thread(pools[1], &thread);
        }

        if (thread != ABT_THREAD_NULL) {
            // We either got a work unit from self private or public pool. Schedule it.
            ABT_self_schedule(thread, ABT_POOL_NULL);
        } else if (num_pools >= 2) {
            // Steal a work unit from the public pool of others

            int steal_from_pool;
            steal_from_pool = (rand() % (num_pools - 2)) + 2;
            ABT_pool_pop_thread(pools[steal_from_pool], &thread);

            if (thread != ABT_THREAD_NULL) {
                ABT_self_schedule(thread, pools[steal_from_pool]);
            }
        }

        if (++work_count >= sched_data->event_freq) {
            work_count = 0;

            ABT_bool stop;
            ABT_sched_has_to_stop(sched, &stop);
            if (stop == ABT_TRUE) {
                break;
            }
            ABT_xstream_check_events(sched);
        }
    }

    free(pools);
}

static int sched_free(ABT_sched sched) {
    sched_data_t *p_data;

    ABT_sched_get_data(sched, (void **)&p_data);
    free(p_data);

    return ABT_SUCCESS;
}

static void create_scheds(int num, ABT_pool *pools, ABT_sched *scheds) {
    ABT_sched_config config;
    ABT_sched_config_var cv_event_freq = {.idx = 0, .type = ABT_SCHED_CONFIG_INT};

    ABT_sched_def sched_def = {
        .type = ABT_SCHED_TYPE_ULT, .init = sched_init, .run = sched_run, .free = sched_free, .get_migr_pool = NULL};

    /* Create a scheduler config */
    ABT_sched_config_create(&config, cv_event_freq, 10, ABT_sched_config_var_end);

    for (int i = 0; i < num; i++) {
        ABT_pool *pools_available;

        pools_available = (ABT_pool *)malloc((1 + num) * sizeof(ABT_pool));
        pools_available[0] = pools[num + i];  // Private of self

        for (int j = 0; j < num; j++) {
            // Public of others (including self)
            pools_available[j + 1] = pools[j];
        }

        // Move public of self to the 2nd position in array
        auto tmp = pools_available[1];
        pools_available[1] = pools_available[i + 1];
        pools_available[i + 1] = tmp;

        // Create the scheduler
        ABT_sched_create(&sched_def, num + 1, pools_available, config, &scheds[i]);

        free(pools_available);
    }

    ABT_sched_config_free(&config);
}
}  // namespace sched_control