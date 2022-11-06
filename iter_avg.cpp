#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "argolibI.hpp"

#define SIZE 48 * 256 * 2048
#define ITERATIONS 64

double *myNew, *myVal;
int n;

long get_usecs() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000000 + t.tv_usec;
}

int ceilDiv(int d) {
    int m = SIZE / d;
    if (m * d == SIZE) {
        return m;
    } else {
        return (m + 1);
    }
}

void recurse(uint64_t low, uint64_t high) {
    if ((high - low) > 512) {
        uint64_t mid = (high + low) / 2;
        /* An async task */
        argolib::TaskHandle task1 = argolib::fork([&]() { recurse(low, mid); });
        // argolib::TaskHandle task2 = argolib::fork([&]() { recurse(mid, high); });
        recurse(mid, high);

        argolib::join(task1);
    } else {
        for (uint64_t j = low; j < high; j++) {
            myNew[j] = (myVal[j - 1] + myVal[j + 1]) / 2.0;
        }
    }
}

void runParallel() {
    for (int i = 0; i < ITERATIONS; i++) {
        argolib::start_tracing();

        argolib::kernel([&]() { recurse(1, SIZE + 1); });

        double* temp = myNew;
        myNew = myVal;
        myVal = temp;

        argolib::stop_tracing();
    }
}

int main(int argc, char** argv) {
    argolib::init(argc, argv);

    myNew = new double[(SIZE + 2)];
    myVal = new double[(SIZE + 2)];
    memset(myNew, 0, sizeof(double) * (SIZE + 2));
    memset(myVal, 0, sizeof(double) * (SIZE + 2));
    myVal[SIZE + 1] = 1.0;

    long start = get_usecs();
    runParallel();
    long end = get_usecs();

    double dur = ((double)(end - start)) / 1000000;
    printf("Time = %.3f\n", dur);

    delete (myNew);
    delete (myVal);

    argolib::finalize();
}