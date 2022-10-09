#include <iostream>

#include "argolib.hpp"

int fib(int n) {
    if (n < 2) return n;

    int x, y;
    argolib::TaskHandle task1 = argolib::fork([&]() { x = fib(n - 1); });
    argolib::TaskHandle task2 = argolib::fork([&]() { y = fib(n - 2); });
    argolib::join(task1, task2);

    return x + y;
}

int main(int argc, char** argv) {
    int n = 30;
    argolib::init(argc, argv);

    int result;
    argolib::kernel([&]() { result = fib(n); });

    std::cout << "Fib(" << n << ") = " << result << std::endl;

    argolib::finalize();
    return 0;
}