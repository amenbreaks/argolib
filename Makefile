argolib: argolib.cpp argolib.hpp
	g++ -o argolib argolib.cpp -labt

clean:
	rm -f argolib