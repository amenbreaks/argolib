#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "abt.h"

#define PUBLIC_POOL_THRESHOLD 4

int sched_use_optimization;

typedef struct {
    uint32_t event_freq;
} sched_data_t;

typedef struct {
    ABT_thread thread;
    ABT_mutex mutex;
} mailbox_t;

mailbox_t *mailboxes;
double *deal_times;
int mailbox_len;

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

    int self_idx = -1;
    ABT_xstream_self_rank(&self_idx);

    while (1) {
        ABT_sched_get_pools(sched, num_pools, 0, pools);
        ABT_thread thread = ABT_THREAD_NULL;

        if (sched_use_optimization) {
            int is_pool_empty;
            ABT_pool_is_empty(pools[0], &is_pool_empty);

            if (is_pool_empty) {
                // If received a task from any other worker, push it to self private pool
                if (mailboxes[self_idx].thread != NULL) {
                    if (mailboxes[self_idx].thread != ABT_THREAD_NULL) {
                        ABT_pool_push_thread_ex(pools[0], mailboxes[self_idx].thread,
                                                ABT_POOL_CONTEXT_OP_THREAD_CREATE);
                    }
                    mailboxes[self_idx].thread = NULL;
                }
            }

            // Try popping from self private pool
            ABT_pool_pop_thread(pools[0], &thread);

            if (thread != ABT_THREAD_NULL) {
                ABT_self_schedule(thread, ABT_POOL_NULL);
            }

            if (deal_times[self_idx] > ABT_get_wtime()) {
                double _start = ABT_get_wtime();

                ABT_pool_is_empty(pools[0], &is_pool_empty);
                if (!is_pool_empty) {
                    int target = (rand() % (mailbox_len));
                    if (target != self_idx && mailboxes[target].thread == NULL) {
                        if (ABT_mutex_lock(mailboxes[target].mutex) == ABT_SUCCESS) {
                            if (mailboxes[target].thread == NULL) {
                                ABT_thread thread_to_send;
                                ABT_pool_pop_thread_ex(pools[0], &thread_to_send, 0);

                                mailboxes[target].thread = thread_to_send;
                            }

                            ABT_mutex_unlock(mailboxes[target].mutex);
                        }
                    }
                }

                double _end = ABT_get_wtime();
                deal_times[self_idx] = _end - (_end - _start) * log(rand());
            }

        } else {
            ABT_pool_pop_thread(pools[self_idx], &thread);
            if (thread != ABT_THREAD_NULL) {
                // Got task from self pool
                ABT_self_schedule(thread, ABT_POOL_NULL);
            } else if (num_pools > 1) {
                // Steal from someone else
                int steal_from = rand() % (num_pools - 1);
                ABT_pool_pop_thread(pools[steal_from], &thread);

                if (thread != ABT_THREAD_NULL) {
                    ABT_self_schedule(thread, pools[steal_from]);
                }
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

static void create_scheds(int num, ABT_pool *pools, ABT_sched *scheds, int use_optimization) {
    // Set the random seed
    srand(time(NULL));

    sched_use_optimization = use_optimization;

    ABT_sched_config config;
    ABT_sched_config_var cv_event_freq = {.idx = 0, .type = ABT_SCHED_CONFIG_INT};

    ABT_sched_def sched_def = {
        .type = ABT_SCHED_TYPE_ULT, .init = sched_init, .run = sched_run, .free = sched_free, .get_migr_pool = NULL};

    /* Create a scheduler config */
    ABT_sched_config_create(&config, cv_event_freq, 10, ABT_sched_config_var_end);

    if (use_optimization) {
        mailbox_len = num;
        mailboxes = (mailbox_t *)calloc(num, sizeof(mailbox_t));
        for (int i = 0; i < mailbox_len; i++) {
            mailboxes[i].thread = NULL;
            ABT_mutex_create(&mailboxes[i].mutex);
        }

        deal_times = (double *)calloc(num, sizeof(double));
        for (int i = 0; i < num; i++) {
            deal_times[i] = ABT_get_wtime();
        }
    }

    for (int i = 0; i < num; i++) {
        int pool_size;
        ABT_pool *pools_available;

        if (use_optimization) {
            pool_size = 1;
            pools_available = (ABT_pool *)malloc(sizeof(ABT_pool));
            pools_available[0] = pools[i];  // Private of self
        } else {
            pool_size = num;
            pools_available = pools;
        }

        // Create the scheduler
        ABT_sched_create(&sched_def, pool_size, pools_available, config, &scheds[i]);

        if (use_optimization) {
            free(pools_available);
        }
    }

    ABT_sched_config_free(&config);
}
