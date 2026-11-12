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

.PHONY: FORCE all build hooks install uninstall doctor setup test test-unit test-stdlib test-e2e test-guide test-conformance test-asan test-tsan fuzz fuzz-replay fuzz-lib bench docs fmt review-check release clean

all: build

HEADERS := $(wildcard src/*.hpp src/*/*.hpp runtime/*.hpp runtime/*/*.hpp runtime/*/*/*.hpp tests/*.hpp tests/unit/*.hpp)

# `cfire` is a real file target (CF-159): it relinks only when sources change or the embedded git SHA does.
.cfire_gitsha: FORCE
	@echo '$(GIT_SHA)' | cmp -s - $@ 2>/dev/null || echo '$(GIT_SHA)' > $@

cfire: src/main.cpp $(HEADERS) .cfire_gitsha
	$(CXX) $(CXXFLAGS) src/main.cpp -o cfire

build: cfire

FORCE:

hooks:
	@git config core.hooksPath .githooks

install: build
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/cfire/runtime
	mkdir -p $(DESTDIR)$(PREFIX)/share/cfire/pkg
	cp cfire $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$ && chmod 755 $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$ && mv -f $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$ $(DESTDIR)$(PREFIX)/bin/cfire
	cp -R runtime/* $(DESTDIR)$(PREFIX)/share/cfire/runtime/
	printf "%s %s\n" "$(CFIRE_VER)" "$(GIT_SHA)" > $(DESTDIR)$(PREFIX)/share/cfire/runtime/cfire-runtime.version
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
	@echo "=== [1/4] Checking Canonical Code Formatting ==="
	./cfire fmt --check examples/
	@echo "✓ Canonical code formatting verified"
	@echo "=== [2/4] Checking Documentation Synchronization ==="
	./cfire doc --check
	@echo "✓ Documentation guides and stdlib references synchronized"
	@echo "=== [3/4] Type-Checking Every Canonical Example (CF-176) ==="
	@set -e; for d in examples/*/; do ./cfire check "$$d" > .cfire_check.log 2>&1 || { cat .cfire_check.log; rm -f .cfire_check.log; echo "cfire check failed: $$d"; exit 1; }; if grep -q "cfire/UNKNOWN-TYPE" .cfire_check.log; then cat .cfire_check.log; rm -f .cfire_check.log; echo "untyped expression in $$d (CF-148)"; exit 1; fi; done; rm -f .cfire_check.log
	@echo "✓ All canonical examples type-check cleanly"
	@echo "=== [4/4] Running Full Conformance Test Suite ==="
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
	$(MAKE) test-guide
	$(MAKE) test-conformance

test-unit: build .cfire_test_runner
	./.cfire_test_runner unit

test-stdlib: build .cfire_test_runner
	./.cfire_test_runner stdlib
	./cfire test tests/stdlib_suite_test.cf

test-e2e: build .cfire_test_runner
	./.cfire_test_runner e2e

test-guide: build
	python3 scripts/run_guide_tests.py

test-conformance: build
	python3 -I scripts/run_conformance.py --self-test
	python3 -I scripts/run_conformance.py

ASAN_CXXFLAGS := -std=c++2b -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) -Iruntime -Isrc
ASAN_LDFLAGS  := -fsanitize=address,undefined -fno-sanitize-recover=undefined $(LDFLAGS)

test-asan: build
	@echo "=== Building & Running All C++ Suites under AddressSanitizer & UndefinedBehaviorSanitizer ==="
	$(CXX) $(ASAN_CXXFLAGS) $(TEST_SRCS) $(ASAN_LDFLAGS) -o .cfire_test_runner_asan
	./.cfire_test_runner_asan
	rm -f .cfire_test_runner_asan
	@echo "=== Running the Conformance Corpus with Generated Programs under ASan/UBSan ==="
	python3 -I scripts/run_conformance.py --run-flags=--sanitize

TSAN_CXXFLAGS := -std=c++2b -O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) -Iruntime -Isrc

test-tsan: build
	@echo "=== Building & Running Runtime Concurrency Tests under ThreadSanitizer ==="
	$(CXX) $(TSAN_CXXFLAGS) tests/tsan_main.cpp -fsanitize=thread $(LDFLAGS) -o .cfire_test_runner_tsan
	./.cfire_test_runner_tsan
	rm -f .cfire_test_runner_tsan
	@echo "=== Running Conformance Concurrency Programs under ThreadSanitizer ==="
	python3 -I scripts/run_conformance.py --filter 'concurrency/*' --run-flags=--sanitize=thread

fuzz: build .cfire_test_runner
	./.cfire_test_runner fuzz
	./cfire test tests/stdlib_suite_test.cf --fuzz-runs 500
	./cfire test pkg/raygui_ext/raygui_ext_test.cf --fuzz-runs 500

# --- Structural fuzz targets (tests/fuzz/fuzz_*.cpp, CF-141) ---
FUZZ_TARGETS   := parser sema fmt http url websocket json_rpc deflate
FUZZ_CF_SEEDS  := examples tests/golden tests/conformance tests/parse_fail scripts/audit/probes tests/fuzz/corpus/sema_crashes
FUZZ_SECONDS   ?= 60
FUZZ_CXX       ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++ /usr/lib/llvm-*/bin/clang++) clang++)

# Replays every committed seed through every target under ASan+UBSan; works without libFuzzer.
fuzz-replay:
	@set -e; for t in $(FUZZ_TARGETS); do \
		echo "=== fuzz-replay $$t"; \
		$(CXX) $(ASAN_CXXFLAGS) tests/fuzz/fuzz_$$t.cpp tests/fuzz/replay_main.cpp $(ASAN_LDFLAGS) -o .fuzz_replay_$$t; \
		case $$t in parser|sema|fmt) seeds="$(FUZZ_CF_SEEDS)";; *) seeds="tests/fuzz/corpus/$$t";; esac; \
		./.fuzz_replay_$$t $$seeds; \
		rm -f .fuzz_replay_$$t; \
	done

# Coverage-guided libFuzzer run of each target for FUZZ_SECONDS (needs LLVM clang with libclang_rt.fuzzer).
fuzz-lib:
	@set -e; for t in $(FUZZ_TARGETS); do \
		echo "=== fuzz-lib $$t ($(FUZZ_SECONDS)s)"; \
		mkdir -p .fuzz_corpus/$$t .fuzz_crashes; \
		$(FUZZ_CXX) -std=c++2b -O1 -g -fsanitize=fuzzer,address,undefined $(LINUX_CXXFLAGS) -Iruntime -Isrc tests/fuzz/fuzz_$$t.cpp $(LDFLAGS) -o .fuzz_$$t; \
		case $$t in parser|sema|fmt) seeds="$(FUZZ_CF_SEEDS)";; *) seeds="tests/fuzz/corpus/$$t";; esac; \
		./.fuzz_$$t -max_total_time=$(FUZZ_SECONDS) -max_len=65536 -artifact_prefix=.fuzz_crashes/$$t- .fuzz_corpus/$$t $$seeds; \
		rm -f .fuzz_$$t; \
	done

bench: build .cfire_test_runner
	./.cfire_test_runner bench
	./cfire bench tests/stdlib_suite_test.cf
	./cfire bench pkg/raygui_ext/raygui_ext_test.cf

clean:
	rm -rf cfire .cfire_gitsha .cfire_test_runner .cfire_test_runner_asan .cfire_test_runner_tsan .fuzz_corpus .fuzz_crashes .fuzz_replay_* .fuzz_* $(TEST_OBJS) examples/*/.cfire tests/.cfire pkg/raygui_ext/.cfire cfire_cross_* cfire_wasm_gui_*
