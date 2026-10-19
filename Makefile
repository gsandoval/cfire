CXX      := clang++
CXXFLAGS := -std=c++2b -O2 -Wall -Wextra

.PHONY: all build test docs fmt clean

all: build

build:
	$(CXX) $(CXXFLAGS) src/main.cpp -o cfire

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
