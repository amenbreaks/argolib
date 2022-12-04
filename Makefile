lpath=/home/ns400/argolib
PCMROOT=/home/nsl400/pcm-202210

# path=-L$(lpath) -I/home/krauzer/argobots-install/include

# all: fib_cpp fib_c libargo.so iter_avg_cpp
all: fib_cpp iter_avg_cpp

argolib.cpp: sched_control.hpp

# libargo: argolib_c.cpp argolib_c.h
# 	g++ -shared -o libargo.so libargo.o
# 	rm -f libargo.o
# # g++ -c -fPIC -o libargo.o argolib_c.cpp

fib_cpp: fib.cpp 
	g++ -I$(PCMROOT)/src -O3 -o fib_cpp fib.cpp -L$(PCMROOT)/build/lib -lpcm -lm -labt -lpthread

# fib_c: fib.c libargo
# 	gcc -O3 $(path) -o fib_c fib.c -lm -lstdc++ -labt -largo

iter_avg_cpp: iter_avg.cpp
	g++ -I$(PCMROOT)/src -g -o iter_avg_cpp iter_avg.cpp -L$(PCMROOT)/build/lib -lpcm -lm -labt

clean:
	rm -f fib_* iter_avg_* libargo.so 

run: fib_cpp
	sudo LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(lpath):$(PCMROOT)/build/lib ./fib_cpp
# LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(lpath) ./fib_c