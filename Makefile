CXX      := clang++
CFIRE_VER := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || echo 0.10.0)
GIT_SHA  ?= $(shell sh -c 'sha=$$(git rev-parse --short=7 HEAD 2>/dev/null) || { echo dev; exit 0; }; git diff --quiet --ignore-submodules HEAD 2>/dev/null && printf "%s" "$$sha" || printf "%s-dirty" "$$sha"')
LINUX_CXXFLAGS := $(if $(filter Linux,$(shell uname -s)),-Wno-builtin-macro-redefined -D__cpp_concepts=202002L,)
CXXFLAGS := -std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) -Iruntime -Isrc
LDFLAGS  := $(if $(filter Darwin,$(shell uname -s)),-framework AudioToolbox -framework CoreFoundation -framework Security,-ldl -lpthread)
PREFIX   ?= /usr/local

TEST_SRCS := tests/unit/test_frontend.cpp \
             tests/unit/test_sema.cpp \
             tests/unit/test_stdlib.cpp \
             tests/e2e/test_canonical.cpp \
             tests/test_main.cpp

TEST_OBJS := $(TEST_SRCS:.cpp=.o)

.PHONY: all build install uninstall doctor setup test test-unit test-stdlib test-e2e fuzz bench docs fmt review-check release clean

all: build

HEADERS := $(wildcard src/*.hpp src/*/*.hpp runtime/*.hpp runtime/*/*.hpp runtime/*/*/*.hpp tests/*.hpp tests/unit/*.hpp)

build: $(HEADERS)
	@git config core.hooksPath .githooks 2>/dev/null || true
	$(CXX) $(CXXFLAGS) src/main.cpp -o cfire

install: build
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/cfire/runtime
	mkdir -p $(DESTDIR)$(PREFIX)/share/cfire/pkg
	cp cfire $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$
	chmod 755 $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$
	mv -f $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$ $(DESTDIR)$(PREFIX)/bin/cfire
	cp -R runtime/* $(DESTDIR)$(PREFIX)/share/cfire/runtime/
	printf "%s %s\n" "$(CFIRE_VER)" "$(GIT_SHA)" > $(DESTDIR)$(PREFIX)/share/cfire/runtime/VERSION
	cp -R pkg/* $(DESTDIR)$(PREFIX)/share/cfire/pkg/
	@echo "✓ Installed cfire $(CFIRE_VER) ($(GIT_SHA)) to $(DESTDIR)$(PREFIX)/bin/cfire and runtime/pkg to $(DESTDIR)$(PREFIX)/share/cfire"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cfire
	rm -rf $(DESTDIR)$(PREFIX)/share/cfire

doctor: build
	./cfire doctor

setup: build
	./cfire setup

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

release:
	@if [ -z "$(VERSION)" ]; then echo "Usage: make release VERSION=x.y.z [FLAGS=--dry-run]"; exit 1; fi
	./scripts/release.sh "$(VERSION)" $(FLAGS)

docs: build
	./cfire doc

fmt: build
	./cfire fmt examples

test: build .cfire_test_runner
	./.cfire_test_runner
	rm -f .cfire_test_runner $(TEST_OBJS)
	./cfire test tests/stdlib_suite_test.cf
	./cfire test pkg/raygui_ext/raygui_ext_test.cf
	./cfire doc --check

test-unit: build .cfire_test_runner
	./.cfire_test_runner unit

test-stdlib: build .cfire_test_runner
	./.cfire_test_runner stdlib
	./cfire test tests/stdlib_suite_test.cf

test-e2e: build .cfire_test_runner
	./.cfire_test_runner e2e

fuzz: build .cfire_test_runner
	./.cfire_test_runner fuzz
	./cfire test tests/stdlib_suite_test.cf --fuzz-runs 500
	./cfire test pkg/raygui_ext/raygui_ext_test.cf --fuzz-runs 500

bench: build .cfire_test_runner
	./.cfire_test_runner bench
	./cfire bench tests/stdlib_suite_test.cf
	./cfire bench pkg/raygui_ext/raygui_ext_test.cf

clean:
	rm -rf cfire .cfire_test_runner $(TEST_OBJS) examples/*/.cfire tests/.cfire pkg/raygui_ext/.cfire cfire_cross_* cfire_wasm_gui_*
