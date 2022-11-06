# argolib

C++ API for Argobots Framework

# Deadline 1

1. Use environment variable ARGOLIB_WORKERS to set the number of execution streams. The default value is 8
2. Use environment variable ARGOLIB_OPTIMIZATION to use the optimized scheduler based on the paper.
   a. If the variable is empty/0, it will use the scheduler created with the basic blocks provided by argolib.
   b. If the varible is set to 1, it will also use the scheduler with private dequeues and Sender-initiated Work Stealing algorithm.

# Deadline 2

1. If you use argolib::start_tracing() && argolib::stop_tracing(), it will automatically run in trace & replay mode.
