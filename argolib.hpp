
#include <functional>

#include "abt.h"
#include "sched_control.hpp"

namespace argolib {
typedef struct {
    std::function<void()> lambda;
} ThreadHandleArgs;

typedef struct {
    ABT_thread thread;
    ThreadHandleArgs args;
} TaskHandle;

void init(int argc, char **argv);

template <typename T>
void kernel(T &&lambda);

template <typename T>
TaskHandle fork(T &&lambda);

template <typename... TaskHandle>
void join(TaskHandle... handles);

void finalize();

}  // namespace argolib
