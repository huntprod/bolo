httpx: httpx.o http.o util.o hash.o io.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wno-error -o $@ $+ $(LDLIBS)

fuzz-http: t/fuzz/http
	./t/afl http

t/fuzz/http: t/fuzz/http.fuzz.o http.fuzz.o hash.fuzz.o util.fuzz.o io.fuzz.o
	afl-gcc $(LDFLAGS) -o $@ $+ $(LDLIBS)

