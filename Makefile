# Makefile for LD_PRELOAD demo project
# Place this at the project root.

CC := gcc
CFLAGS := -Wall -Wextra -O2
PICFLAGS := -fPIC
LDFLAGS := -shared

SRC_LIB := src/sandbox_preload.c
LIB := build/libsandbox.so

TEST_SRC := test/test_open.c
TEST_BIN := build/test_open

.PHONY: all lib test debug clean run preload_run

all: $(LIB) $(TEST_BIN)

$(LIB): $(SRC_LIB) | build
	$(CC) $(CFLAGS) $(PICFLAGS) $(LDFLAGS) -o $@ $<

$(TEST_BIN): $(TEST_SRC) | build
	$(CC) $(CFLAGS) -o $@ $<

lib: $(LIB)

test: $(TEST_BIN)

# Debug build with sanitizers
debug: CFLAGS += -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean all

# Run the test program normally
run: $(TEST_BIN)
	./$(TEST_BIN)

# Run the test program with the LD_PRELOAD library
preload_run: $(LIB) $(TEST_BIN)
	LD_PRELOAD=$(CURDIR)/$(LIB) ./$(TEST_BIN)

# Ensure build directory exists
build:
	mkdir -p build

clean:
	rm -rf build
C2_SRC := src/c2_server.c
C2_BIN := build/c2_server

$(C2_BIN): $(C2_SRC) | build
	$(CC) $(CFLAGS) -pthread -o $@ $<

c2: $(C2_BIN)

# Full demo
demo: c2 lib test
	./$(C2_BIN) &
	sleep 1
	sudo make preload_run
