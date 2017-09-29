CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter
TEST_CFLAGS := -g -DTEST -fprofile-arcs -ftest-coverage -It

all: main bolo

bolo: bolo.o debug.o sha.o time.o util.o page.o tblock.o tslab.o db.o hash.o btree.o
	$(CC) -o $@ $+

main: main.o debug.o sha.o time.o util.o page.o tblock.o tslab.o
	$(CC) -o $@ $+

clean:
	rm -f *.o
	rm -f main
	rm -f sha time
	rm -f *.gcno *.gcda
	rm -f lcov.info

PAGE_DEPS  := util.o
BTREE_DEPS := page.o $(PAGE_DEPS)
DB_DEPS    := btree.o $(BTREE_DEPS)
DB_DEPS    += hash.o
DB_DEPS    += sha.o
DB_DEPS    += tblock.o tslab.o

test: check
check: $(PAGE_DEPS) $(BTREE_DEPS) $(DB_DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bits  bits.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o util  util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o hash  hash.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o page  page.c   $(PAGE_DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o btree btree.c  $(BTREE_DEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o sha   sha.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o time  time.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o db    db.c     $(DB_DEPS)
	prove -v ./bits ./util ./hash ./page ./btree ./sha ./time ./db

memtest: check
	t/vg ./bits ./util ./hash ./page ./btree ./sha ./time ./db
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
	#find . -name '*.[ch]' | xargs grep -rin -C4 --color fixme

.PHONY: coverage
