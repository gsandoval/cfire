CXX      := clang++
CXXFLAGS := -std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality -Iruntime -Isrc

TEST_SRCS := tests/unit/test_frontend.cpp \
             tests/unit/test_sema.cpp \
             tests/unit/test_stdlib.cpp \
             tests/e2e/test_canonical.cpp \
             tests/benchmarks/test_selfhost.cpp \
             tests/test_main.cpp

TEST_OBJS := $(TEST_SRCS:.cpp=.o)

.PHONY: all build test test-unit test-stdlib test-e2e test-selfhost docs fmt dogfood review-check clean

all: build

build:
	$(CXX) $(CXXFLAGS) src/main.cpp -o cfire

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

.cfire_test_runner: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) -o $@

dogfood: build
	mkdir -p src/bootstrap
	./cfire emit-cpp src/lexer.cf > src/bootstrap/lexer.generated.hpp
	./cfire emit-cpp src/formatter.cf > src/bootstrap/formatter.generated.hpp
	./cfire emit-cpp src/ast.cf > src/bootstrap/ast.generated.hpp
	./cfire emit-cpp src/refactor.cf > src/bootstrap/refactor.generated.hpp
	./cfire emit-cpp src/docgen.cf > src/bootstrap/docgen.generated.hpp
	./cfire emit-cpp src/parser.cf > src/bootstrap/parser.generated.hpp
	./cfire emit-cpp src/sema.cf > src/bootstrap/sema.generated.hpp
	./cfire emit-cpp src/codegen.cf > src/bootstrap/codegen.generated.hpp
	./cfire emit-cpp src/buildsys.cf > src/bootstrap/buildsys.generated.hpp
	./cfire emit-cpp src/lsp.cf > src/bootstrap/lsp.generated.hpp
	./cfire emit-cpp src/main.cf > src/bootstrap/main.generated.hpp
	./cfire emit-cpp src/clang_bridge.cf > src/bootstrap/clang_bridge.generated.hpp
	$(MAKE) test

review-check: build
	@echo "=== [1/4] Checking Canonical Code Formatting ==="
	./cfire fmt --check src/ examples/
	@echo "=== [2/4] Checking Documentation Synchronization ==="
	./cfire doc --check
	@echo "=== [3/4] Re-transpiling and Dogfooding Self-Hosted Compiler ==="
	$(MAKE) dogfood
	@echo "=== [4/4] Running Full Conformance Test Suite ==="
	$(MAKE) test
	@echo "✓ All Senior Engineer Review Quality Gates PASSED"

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

test-selfhost: build .cfire_test_runner
	./.cfire_test_runner selfhost

clean:
	rm -rf cfire .cfire_test_runner $(TEST_OBJS) examples/*/.cfire
