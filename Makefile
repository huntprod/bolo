CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter
TEST_CFLAGS := -g -DTEST -fprofile-arcs -ftest-coverage -It

all: main bolo

bolo: bolo.o debug.o sha.o time.o util.o page.o tblock.o tslab.o
	$(CC) -o $@ $+

main: main.o debug.o sha.o time.o util.o page.o tblock.o tslab.o
	$(CC) -o $@ $+

clean:
	rm -f *.o
	rm -f main
	rm -f sha time
	rm -f *.gcno *.gcda
	rm -f lcov.info

test: check
check: page.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bits  bits.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o hash  hash.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o page  page.c util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o btree btree.c page.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o sha   sha.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o time  time.c
	prove -v ./bits ./hash ./page ./btree ./sha ./time

memtest: check
	t/vg ./bits ./hash ./page ./btree ./sha ./time
	@echo "No memory leaks detected"

coverage:
	rm -rf coverage/
	lcov --capture --directory . --output-file lcov.info
	genhtml -o coverage/ lcov.info
	rm -f lcov.info

copycov: clean test coverage
	rm -rf /vagrant/coverage/
	cp -a coverage/ /vagrant/coverage/

# full test suite
sure: memtest

.PHONY: coverage
