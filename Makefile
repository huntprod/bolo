CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter
TEST_CFLAGS := -g -DTEST -fprofile-arcs -ftest-coverage -It
LDLIBS += -lm

ifeq ($(PROF),yes)
	# BOTH -p and -pg seem to be necessary, if you want
	# the gmon.out file to include timing information.
	CFLAGS  += -p -pg
	LDFLAGS += -p -pg
endif

TESTS := bits util
TESTS += rsv log cfg
TESTS += hash page btree
TESTS += sha time
TESTS += tags query db
TESTS += bqip
TESTS += ingest

all: bolo
bolo: bolo.o debug.o sha.o time.o util.o page.o tblock.o tslab.o db.o hash.o \
      btree.o log.o tags.o query.o rsv.o bql/bql.a bqip.o net.o ingest.o cfg.o \
      \
      bolo-help.o bolo-version.o bolo-core.o bolo-dbinfo.o bolo-idxinfo.o bolo-slabinfo.o \
      bolo-import.o bolo-parse.o bolo-query.o
	$(CC) $(LDFLAGS) -o $@ $+ $(LDLIBS)

bqlx: bql/main.o bql/bql.a util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wno-error -o $@ $+ $(LDLIBS)

clean:
	rm -f *.o *.gcno *.gcda
	rm -f bql/*.o bql/*.gcno bql/*.gcda
	rm -f $(TESTS)
	rm -f lcov.info

realclean: clean
	rm -rf t/data/db

distclean: realclean
	rm -f bql/grammar.c bql/lexer.c

test: check
check: testdata util.o debug.o log.o page.o btree.o hash.o rsv.o sha.o tblock.o tslab.o tags.o bql/bql.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bits  bits.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o util  util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o rsv   rsv.c    util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o log   log.c    util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o cfg   cfg.c    log.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o hash  hash.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o page  page.c   util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o btree btree.c  page.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o sha   sha.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o time  time.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o tags  tags.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o query query.c  hash.o util.o bql/bql.a rsv.o btree.o page.o db.o sha.o tblock.o tslab.o log.o tags.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o db    db.c     btree.o page.o util.o hash.o sha.o tblock.o tslab.o log.o tags.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bqip  bqip.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o ingest ingest.c debug.o tags.o
	prove -v $(addprefix ./,$(TESTS))

testdata: bolo t/data/db/1/main.db
t/data/db/1/main.db: t/data/db1.dat
	mkdir -p t/data/db
	./t/stream < $< | ./bolo import t/data/db/1

memtest: check
	t/vg $(addprefix ./,$(TESTS))
	@echo "No memory leaks detected"

%.fuzz.o: %.c
	afl-gcc $(CPPFLAGS) $(CFLAGS) -c -o $@ $+

fuzz-bqip: t/fuzz/bqip
	./t/afl bqip

t/fuzz/bqip: t/fuzz/bqip.fuzz.o bqip.fuzz.o
	afl-gcc $(LDFLAGS) -o $@ $+ $(LDLIBS)

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
	find . -name '*.[ch]' -not -path '*/ice/*' | xargs grep -rin fixme

bql/lexer.c: bql/lexer.l
	flex -FTvs -o $@ $<
	#patch -p1 <bql/flex.patch
bql/lexer.o: bql/grammar.h bql/lexer.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ bql/lexer.c

bql/grammar.h: bql/grammar.y
	bison --report all --graph=bql/graph --defines=bql/grammar.h --output bql/grammar.c bql/grammar.y
bql/grammar.c: bql/grammar.y
	bison --report all --graph=bql/graph --defines=bql/grammar.h --output bql/grammar.c bql/grammar.y
bql/grammar.o: bql/grammar.h bql/grammar.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ bql/grammar.c

bql/bql.a: bql/grammar.o bql/lexer.o
	ar cr $@ $+


.PHONY: all clean distclean test check memtest coverage copycov ccov sure fixme

ll: l/bql.ll.o l/main.o
	$(CC) -o $@ $+
