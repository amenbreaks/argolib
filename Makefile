path=-L/home/krauzer/argobots/rada -I/home/krauzer/argobots-install/include
fib: fib.cpp argolib.hpp sched_control.hpp
	g++ $(path) -o fib fib.cpp -labt

clean:
	rm -f fib
