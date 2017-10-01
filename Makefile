CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter
TEST_CFLAGS := -g -DTEST -fprofile-arcs -ftest-coverage -It

all: bolo

bolo: bolo.o debug.o sha.o time.o util.o page.o tblock.o tslab.o db.o hash.o btree.o log.o
	$(CC) -o $@ $+

clean:
	rm -f *.o
	rm -f sha time
	rm -f *.gcno *.gcda
	rm -f lcov.info

test: check
check: util.o log.o page.o btree.o hash.o sha.o tblock.o tslab.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bits  bits.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o util  util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o log   log.c    util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o cfg   cfg.c    log.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o hash  hash.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o page  page.c   util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o btree btree.c  page.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o sha   sha.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o time  time.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o db    db.c     btree.o page.o util.o hash.o sha.o tblock.o tslab.o log.o
	prove -v ./bits ./util ./log ./cfg ./hash ./page ./btree ./sha ./time ./db

memtest: check
	t/vg ./bits ./util ./log ./cfg ./hash ./page ./btree ./sha ./time ./db
	@echo "No memory leaks detected"

coverage:
	rm -rf coverage/
	lcov --capture --directory . --output-file lcov.info
	genhtml -o coverage/ lcov.info
	rm -f lcov.info

copycov: coverage
	rm -rf /vagrant/coverage/
	cp -a coverage/ /vagrant/coverage/

ccov: clean test copycov

# full test suite
sure: memtest

fixme:
	find . -name '*.[ch]' | xargs grep -rin fixme

.PHONY: coverage
