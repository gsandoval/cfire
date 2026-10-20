CXX      := clang++
CXXFLAGS := -std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality -Iruntime -Isrc

.PHONY: all build test docs fmt dogfood clean

all: build

build:
	$(CXX) $(CXXFLAGS) src/main.cpp -o cfire

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
	$(MAKE) test

docs: build
	./cfire doc

fmt: build
	./cfire fmt examples

test: build
	$(CXX) $(CXXFLAGS) tests/conformance_test.cpp -o .cfire_test_runner
	./.cfire_test_runner
	rm -f .cfire_test_runner
	./cfire doc --check

clean:
	rm -rf cfire .cfire_test_runner examples/*/.cfire
