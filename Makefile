lpath=/home/priyansh/Desktop/argolib
path=-L$(lpath) -I/home/krauzer/argobots-install/include

all: fib_cpp fib_c libargo.so

argolib.cpp: sched_control.h

libargo: argolib.cpp argolib.hpp argolib.c argolib.h
	gcc -c -fPIC -o libargo.o argolib.c
	gcc -shared -o libargo.so libargo.o
	rm -f libargo.o

fib_cpp: fib.cpp 
	g++ -O3 $(path) -o fib_cpp fib.cpp -lm -labt

fib_c: fib.c libargo
	gcc -O3 $(path) -o fib_c fib.c -lm -lstdc++ -labt -largo

clean:
	rm -f fib_* libargo.so

run: fib_c fib_cpp
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(lpath) ./fib_c
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(lpath) ./fib_cpp