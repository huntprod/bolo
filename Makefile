default: all

CFLAGS += -Wall -Wextra -Wpedantic -Wunused -Wunused-result -Wno-unused-parameter -std=gnu99
TEST_CFLAGS := -g -DTEST -fprofile-arcs -ftest-coverage -It
LDLIBS += -lm -lpthread

ifeq ($(PROF),yes)
	# BOTH -p and -pg seem to be necessary, if you want
	# the gmon.out file to include timing information.
	CFLAGS  += -p -pg
	LDFLAGS += -p -pg
endif

TESTS := bits util
TESTS += cf cfg
TESTS += hash page btree
TESTS += sha time
TESTS += tags query db
TESTS += bqip
TESTS += ingest

COLLECTORS :=
COLLECTORS += linux
linux: collectors/linux.o hash.o time.o util.o
	$(CC) $(LDFLAGS) -o $@ $+ -lpcre

COLLECTORS += process
process: collectors/process
	cp $+ $@

all: bolo $(COLLECTORS)
everything: all api/api

bolo: bolo.o sha.o time.o util.o page.o tblock.o tslab.o db.o hash.o \
      btree.o tags.o query.o cf.o bql/bql.a bqip.o net.o fdpoll.o ingest.o cfg.o \
      \
      bolo-help.o bolo-version.o bolo-core.o bolo-dbinfo.o bolo-idxinfo.o bolo-slabinfo.o \
      bolo-import.o bolo-parse.o bolo-query.o bolo-init.o bolo-agent.o
	$(CC) $(LDFLAGS) -o $@ $+ $(LDLIBS)

api/api:
	cd api && go build .

dist:
	VERSION=$$(./version.sh); \
	rm -rf bolo-$${VERSION}; \
	mkdir -p bolo-$${VERSION}; \
	cp -a Makefile *.h *.c bql/ t/ HACKING bolo-$${VERSION}/; \
	mkdir -p bolo-$${VERSION}/collectors; \
	cp -a collectors/linux.c collectors/process bolo-$${VERSION}/collectors/; \
	mkdir -p bolo-$${VERSION}/docs; \
	cp -a docs/collectors bolo-$${VERSION}/docs/; \
	find bolo-$${VERSION}/ -name '*.o' -exec rm \{} \;; \
	tar -cjvf bolo-$${VERSION}.tar.bz2 bolo-$${VERSION}/

distcheck: dist
	VERSION=$$(./version.sh); \
	rm -rf _distcheck; mkdir -p _distcheck; \
	cd _distcheck; \
	tar -xjvf ../bolo-$${VERSION}.tar.bz2; \
	cd bolo-$${VERSION}; \
	make test; make; \
	cd ../..; \
	rm -rf _distcheck

clean:
	rm -f bolo $(COLLECTORS)
	rm -f *.o *.gcno *.gcda
	rm -f bql/*.o bql/*.gcno bql/*.gcda
	rm -f $(TESTS)
	rm -f lcov.info
	rm -f slow reexec

realclean: clean
	rm -rf t/data/db

distclean: realclean
	rm -f bql/grammar.c bql/lexer.c

test: check
check: testdata util.o page.o btree.o hash.o cf.o sha.o tblock.o tslab.o tags.o bql/bql.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bits  bits.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o util  util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o cf    cf.c     util.o -lm
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o cfg   cfg.c    hash.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o hash  hash.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o page  page.c   util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o btree btree.c  page.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o sha   sha.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o time  time.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o tags  tags.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o query query.c  hash.o util.o bql/bql.a cf.o btree.o page.o db.o sha.o tblock.o tslab.o tags.o -lm
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o db    db.c     btree.o page.o util.o hash.o sha.o tblock.o tslab.o tags.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o bqip  bqip.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o ingest ingest.c util.o tags.o
	prove -v $(addprefix ./,$(TESTS))

testdata: bolo t/data/db/1/main.db
t/data/db/1/main.db: t/data/db1.dat
	mkdir -p t/data/db
	./t/stream < $< | ./bolo import t/data/db/1 --key decafbad

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

docker: docker-core docker-web
docker-core: bolo reexec
	docker/build setup docker/bolo/core
	docker/build copy  docker/bolo/core bolo reexec docker/bolo/core/addon/*
	docker/build libs  docker/bolo/core bolo reexec
	docker/build clean docker/bolo/core
	docker build -t bolo/core:latest docker/bolo/core
docker-web: api/api reexec
	docker/build setup docker/bolo/web
	docker/build copy  docker/bolo/web reexec api/api api/htdocs docker/bolo/web/addon/*
	docker/build libs  docker/bolo/web reexec api
	docker/build clean docker/bolo/web
	docker build -t bolo/web:latest docker/bolo/web

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


.PHONY: all everything clean distclean test check memtest coverage copycov ccov sure fixme

ll: l/bql.ll.o l/main.o
	$(CC) -o $@ $+
