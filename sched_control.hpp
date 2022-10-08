#include "abt.h"
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

namespace sched_control{
    typedef struct {
        uint32_t event_freq;
    } sched_data_t;

    static int sched_init(ABT_sched sched, ABT_sched_config config)
    {
        sched_data_t *p_data = (sched_data_t *)calloc(1, sizeof(sched_data_t));

        ABT_sched_config_read(config, 1, &p_data->event_freq);
        ABT_sched_set_data(sched, (void *)p_data);

        return ABT_SUCCESS;
    }

    static void sched_run(ABT_sched sched)
    {
        uint32_t work_count = 0;
        sched_data_t *p_data;
        int num_pools;
        ABT_pool *pools;
        int target;
        ABT_bool stop;
        unsigned seed = time(NULL);

        ABT_sched_get_data(sched, (void **)&p_data);
        ABT_sched_get_num_pools(sched, &num_pools);
        pools = (ABT_pool *)malloc(num_pools * sizeof(ABT_pool));
        ABT_sched_get_pools(sched, num_pools, 0, pools);

        while (1) {
            /* Execute one work unit from the scheduler's pool */
            ABT_thread thread;
            ABT_pool_pop_thread(pools[0], &thread);
            if (thread != ABT_THREAD_NULL) {
                /* "thread" is associated with its original pool (pools[0]). */
                ABT_self_schedule(thread, ABT_POOL_NULL);
            } else if (num_pools > 1) {
                /* Steal a work unit from other pools */
                target =
                    (num_pools == 2) ? 1 : (rand_r(&seed) % (num_pools - 1) + 1);
                ABT_pool_pop_thread(pools[target], &thread);
                if (thread != ABT_THREAD_NULL) {
                    /* "thread" is associated with its original pool
                    * (pools[target]). */
                    ABT_self_schedule(thread, pools[target]);
                }
            }

            if (++work_count >= p_data->event_freq) {
                work_count = 0;
                ABT_sched_has_to_stop(sched, &stop);
                if (stop == ABT_TRUE){
                    
                    break;
                }
                ABT_xstream_check_events(sched);
            }
        }

        free(pools);
    }

    static int sched_free(ABT_sched sched)
    {
        sched_data_t *p_data;

        ABT_sched_get_data(sched, (void **)&p_data);
        free(p_data);

        return ABT_SUCCESS;
    }

    static void create_scheds(int num, ABT_pool *pools, ABT_sched *scheds)
    {
        ABT_sched_config config;
        ABT_pool *my_pools;
        int i, k;

        ABT_sched_config_var cv_event_freq = { .idx = 0,
                                            .type = ABT_SCHED_CONFIG_INT };

        ABT_sched_def sched_def = { .type = ABT_SCHED_TYPE_ULT,
                                    .init = sched_init,
                                    .run = sched_run,
                                    .free = sched_free,
                                    .get_migr_pool = NULL };

        /* Create a scheduler config */
        ABT_sched_config_create(&config, cv_event_freq, 10,
                                ABT_sched_config_var_end);

        my_pools = (ABT_pool *)malloc(num * sizeof(ABT_pool));
        for (i = 0; i < num; i++) {
            for (k = 0; k < num; k++) {
                my_pools[k] = pools[(i + k) % num];
            }

            ABT_sched_create(&sched_def, num, my_pools, config, &scheds[i]);
        }
        free(my_pools);

        ABT_sched_config_free(&config);
    }
}