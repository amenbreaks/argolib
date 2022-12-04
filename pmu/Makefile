#Command to run executable after doing make
PCMROOT=/home/nsl400/pcm-202210

TARGETS := vecadd

all: clean $(TARGETS)
clean:
	rm -f $(TARGETS) 2>/dev/null
%: %.cpp
	g++ -I$(PCMROOT)/src -O3 -o $@ $^ -L$(PCMROOT)/build/lib -lpcm

run:
	sudo LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/nsl400/pcm-202210/build/lib ./vecadd
