lpath=/home/priyansh/Desktop/argolib
path=-L$(lpath) -I/home/krauzer/argobots-install/include

all: fib_cpp fib_c libargo.so iter_avg_cpp

argolib.cpp: sched_control.hpp

libargo: argolib_c.cpp argolib_c.h
	g++ -c -fPIC -o libargo.o argolib_c.cpp
	g++ -shared -o libargo.so libargo.o
	rm -f libargo.o

fib_cpp: fib.cpp 
	g++ -O3 $(path) -o fib_cpp fib.cpp -lm -labt

fib_c: fib.c libargo
	gcc -O3 $(path) -o fib_c fib.c -lm -lstdc++ -labt -largo

iter_avg_cpp: iter_avg.cpp
	g++ -O3 $(path) -o iter_avg_cpp iter_avg.cpp -lm -labt

clean:
	rm -f fib_* iter_avg_* libargo.so 

run: fib_c fib_cpp
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(lpath) ./fib_c
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(lpath) ./fib_cpp