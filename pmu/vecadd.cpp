/*
 * Author: Vivek Kumar
 * vivekk@iiitd.ac.in
 */
#include <iostream>
#include <assert.h>
#include <chrono>
#include "pcm.h"

int main(int argc, char** argv) {
  const int size = argc>1?atoi(argv[1]):1024*1024;
  int* a = new int[size];
  int* b = new int[size];
  int* c = new int[size];
  std::fill(a, a+size, 1);
  std::fill(b, b+size, 2);
  std::fill(c, c+size, 0);
  logger::start();
  for(int i=0; i<size; i++) {
    c[i] = a[i] + b[i];
  }
  double retval = logger::end();
  std::cout << retval << std::endl;
  sleep(1);
  retval = logger::end();
  std::cout << retval << std::endl;
  sleep(1);
  retval = logger::end();
  std::cout << retval << std::endl;
  sleep(1);
  retval = logger::end();
  std::cout << retval << std::endl;
  logger::___pcm->cleanup();
  delete []a;
  delete []b;
  delete []c;
  return 0;
}
