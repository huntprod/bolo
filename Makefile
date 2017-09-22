CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter

all: main

main: main.o debug.o tsdb.o sha.o time.o
	$(CC) -o $@ $+

clean:
	rm -f *.o
	rm -f main
	rm -f sha time

test: check
check:
	$(CC) -DTEST -o sha  sha.c
	$(CC) -DTEST -o time time.c
	@echo "------------------------"
	@./sha
	@./time
	@echo "------------------------"
	@echo "tests passed"
