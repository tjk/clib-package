# bad target l0l... add libgit2 as dependency properly
deps/libgit2:
	git clone https://github.com/libgit2/libgit2.git deps/libgit2 && \
	  cd deps/libgit2 && mkdir build && cd build && cmake .. && cmake --build .

CC ?= cc
VALGRIND ?= valgrind
TEST_RUNNER ?=

SRC = $(wildcard src/*.c)
DEPS += $(wildcard deps/*/*.c)
OBJS = $(SRC:.c=.o) $(DEPS:.c=.o)
TEST_SRC = $(wildcard test/*.c)
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_BIN = $(TEST_SRC:.c=)

CFLAGS = -std=c99 -Wall -Isrc -Ideps
LDFLAGS = -lcurl -Ldeps/libgit2/build -lgit2
VALGRIND_OPTS ?= --leak-check=full --error-exitcode=3

.DEFAULT_GOAL := test

test: $(TEST_BIN)
	$(foreach t, $^, $(TEST_RUNNER) ./$(t) || exit 1;)

valgrind: TEST_RUNNER=$(VALGRIND) $(VALGRIND_OPTS)
valgrind: test

example: example.o $(OBJS)

test/%: test/%.o $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJS)
	rm -f $(TEST_OBJ)
	rm -f $(TEST_BIN)
	rm -rf test/fixtures
	rm -rf deps/libgit2

.PHONY: test valgrind clean
