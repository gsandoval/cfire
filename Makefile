CXX      := clang++
CXXFLAGS := -std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality -Iruntime -Isrc
LDFLAGS  := $(if $(filter Darwin,$(shell uname -s)),-framework AudioToolbox -framework CoreFoundation,-ldl -lpthread)

TEST_SRCS := tests/unit/test_frontend.cpp \
             tests/unit/test_sema.cpp \
             tests/unit/test_stdlib.cpp \
             tests/e2e/test_canonical.cpp \
             tests/test_main.cpp

TEST_OBJS := $(TEST_SRCS:.cpp=.o)

.PHONY: all build test test-unit test-stdlib test-e2e fuzz bench docs fmt review-check clean

all: build

HEADERS := $(wildcard src/*.hpp src/*/*.hpp runtime/*.hpp runtime/*/*.hpp runtime/*/*/*.hpp)

build: $(HEADERS)
	$(CXX) $(CXXFLAGS) src/main.cpp -o cfire

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.cfire_test_runner: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(LDFLAGS) -o $@

review-check: build
	@echo "=== [1/3] Checking Canonical Code Formatting ==="
	./cfire fmt --check examples/
	@echo "✓ Canonical code formatting verified"
	@echo "=== [2/3] Checking Documentation Synchronization ==="
	./cfire doc --check
	@echo "✓ Documentation guides and stdlib references synchronized"
	@echo "=== [3/3] Running Full Conformance Test Suite ==="
	$(MAKE) test
	@echo "✓ Conformance test suites passed"

docs: build
	./cfire doc

fmt: build
	./cfire fmt examples

test: build .cfire_test_runner
	./.cfire_test_runner
	rm -f .cfire_test_runner $(TEST_OBJS)
	./cfire doc --check

test-unit: build .cfire_test_runner
	./.cfire_test_runner unit

test-stdlib: build .cfire_test_runner
	./.cfire_test_runner stdlib

test-e2e: build .cfire_test_runner
	./.cfire_test_runner e2e

fuzz: build .cfire_test_runner
	./.cfire_test_runner fuzz

bench: build .cfire_test_runner
	./.cfire_test_runner bench

clean:
	rm -rf cfire .cfire_test_runner $(TEST_OBJS) examples/*/.cfire cfire_cross_*
