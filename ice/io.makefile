TESTS := io

check:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_CFLAGS) -o io    io.c
