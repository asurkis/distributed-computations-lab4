build: *.h *.c
	clang -L. -lruntime -Wall -pedantic -std=c99 *.c
run: build
	LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:. ./a.out -p 3 10 20 30
clean:
	rm -rf a.out *.log
