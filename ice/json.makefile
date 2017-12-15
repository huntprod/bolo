TESTS += json

check: util.io io.io
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o json  json.c  util.o io.o $(LDLIBS)

fuzz-json: t/fuzz/json
	./t/afl json

t/fuzz/json: t/fuzz/json.fuzz.o json.fuzz.o util.fuzz.o io.fuzz.o
	afl-gcc $(LDFLAGS) -o $@ $+ $(LDLIBS)


docs/diag/json.png: docs/diag/json.dot
	dot -Tpng <$< >$@

