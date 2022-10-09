#include "argolib.h"

typedef struct {
    int n;
    int ret;
} fibargs_t;

void fib(void *args) {
    int n = ((fibargs_t *) args )-> n;
    int ret = ((fibargs_t *) args )-> ret;
    if (n < 2) return;

    fibargs_t x = {n-1, 0};
    fibargs_t y = {n-2, 0};
    TaskHandle *task1 = argolib_fork(&fib, &x);
    TaskHandle *task2 = argolib_fork(&fib, &y);

    TaskHandle** threadlist = (TaskHandle **) malloc(sizeof(TaskHandle **) * 2);
    threadlist[0] = task1;
    threadlist[1] = task2;

    argolib_join(threadlist, 2);

    free(threadlist);

    ret = x.ret + y.ret;
    return;
}

int main(int argc, char** argv) {
    int n = 25;
    argolib_init(argc, argv);

    int result;
    fibargs_t args = {n, 0};
    argolib_kernel(fib, &args);
    result = args.ret;

    printf("Fib(%d) = %d",n, result);

    argolib_finalize();
    return 0;
}