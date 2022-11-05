#include <stdlib.h>
#include <unistd.h>

#include "abt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef ABT_thread TaskHandle;
typedef void (*fork_t)(void *args);

void argolib_init(int argc, char **argv);

void argolib_kernel(fork_t fptr, void *args);

TaskHandle *argolib_fork(fork_t fptr, void *args);

void argolib_join(TaskHandle **handles, int size);

void argolib_finalize();

#ifdef __cplusplus
}
#endif
