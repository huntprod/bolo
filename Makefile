CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter
TEST_CFLAGS := -g -DTEST

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
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bits  bits.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o btree btree.c page.c util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o sha   sha.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o time  time.c
	@echo "------------------------"
	@./bits
	@./btree
	@./sha
	@./time
	@echo "------------------------"
	@echo "tests passed"
