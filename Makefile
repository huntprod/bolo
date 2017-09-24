CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter

all: main bolo

bolo: bolo.o debug.o sha.o time.o util.o page.o tblock.o tslab.o
	$(CC) -o $@ $+

main: main.o debug.o sha.o time.o util.o page.o tblock.o tslab.o
	$(CC) -o $@ $+

clean:
	rm -f *.o
	rm -f main
	rm -f sha time

test: check
check:
	$(CC) -DTEST -o bits bits.c
	$(CC) -DTEST -o sha  sha.c
	$(CC) -DTEST -o time time.c
	@echo "------------------------"
	@./bits
	@./sha
	@./time
	@echo "------------------------"
	@echo "tests passed"
