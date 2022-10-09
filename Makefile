path=-L/home/krauzer/argobots/rada -I/home/krauzer/argobots-install/include

all: fib_cpp fib_c

fib_cpp: fib.cpp argolib.hpp sched_control.h
	g++ -O3 $(path) -o fib_cpp fib.cpp -labt

fib_c: fib.c argolib.h sched_control.h
	gcc -O3 $(path) -o fib_c fib.c -labt
clean:
	rm -f fib_*
