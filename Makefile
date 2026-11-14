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

.PHONY: FORCE all build hooks install uninstall doctor setup check check-warnings check-deep check-platforms check-all vm-setup-linux vm-setup-windows test test-unit test-stdlib test-e2e test-guide test-conformance test-asan test-tsan test-vm-linux test-vm-windows fuzz fuzz-replay fuzz-lib bench docs fmt review-check release clean

all: build

HEADERS := $(wildcard src/*.hpp src/*/*.hpp runtime/*.hpp runtime/*/*.hpp runtime/*/*/*.hpp tests/*.hpp tests/unit/*.hpp tools/*/*.hpp)

# `cfire` is a real file target (CF-159): it relinks only when sources change or the embedded git SHA does.
.cfire_gitsha: FORCE
	@echo '$(GIT_SHA)' | cmp -s - $@ 2>/dev/null || echo '$(GIT_SHA)' > $@

# Each compile keeps its diagnostics in a .log next to its output, so `make check` can prove zero warnings even when
# nothing needed rebuilding (CF-186).
cfire: src/main.cpp $(HEADERS) .cfire_gitsha
	@$(CXX) $(CXXFLAGS) src/main.cpp -o cfire 2> .cfire_build.log; rc=$$?; cat .cfire_build.log >&2; echo "$(CXX) ... src/main.cpp -o cfire (rc=$$rc)"; exit $$rc

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
	@$(CXX) $(CXXFLAGS) -c $< -o $@ 2> $@.log; rc=$$?; cat $@.log >&2; echo "$(CXX) ... -c $< (rc=$$rc)"; exit $$rc

.cfire_test_runner: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(LDFLAGS) -o $@

# --- CF-186: the three test tiers. These are the only commands to remember (docs/planning/README.md):
#   make check            every commit (the pre-commit hook runs it)
#   make check-deep       sanitizers and fuzz replay; required before a CF item is marked done
#   make check-platforms  the Linux and Windows VMs; required before a CF item is marked done, and before a release
#   make check-all        all three in order, then a summary table; its summary goes in active.md
check:
	@[ -f .cfire_build.log ] || rm -f cfire
	@for o in $(TEST_OBJS); do [ -f $$o.log ] || rm -f $$o; done
	@$(MAKE) --no-print-directory build .cfire_test_runner
	@$(MAKE) --no-print-directory check-warnings
	@echo "=== Canonical formatting"
	./cfire fmt --check examples/
	@echo "=== Every canonical example type-checks (CF-176, CF-148)"
	@set -e; for d in examples/*/; do ./cfire check "$$d" > .cfire_check.log 2>&1 || { cat .cfire_check.log; rm -f .cfire_check.log; echo "cfire check failed: $$d"; exit 1; }; if grep -q "cfire/UNKNOWN-TYPE" .cfire_check.log; then cat .cfire_check.log; rm -f .cfire_check.log; echo "untyped expression in $$d (CF-148)"; exit 1; fi; done; rm -f .cfire_check.log
	@$(MAKE) --no-print-directory test
	@echo "✓ make check passed"

check-warnings:
	@n=$$(cat .cfire_build.log $(TEST_OBJS:=.log) $(wildcard .vmtest.log) 2>/dev/null | grep -c 'warning:'); \
	if [ "$$n" != "0" ]; then cat .cfire_build.log $(TEST_OBJS:=.log) $(wildcard .vmtest.log) 2>/dev/null | grep -A4 'warning:'; echo "✗ $$n C++ compiler warning(s); the gate requires zero"; exit 1; fi; \
	echo "✓ zero C++ compiler warnings"

check-deep: test-asan test-tsan fuzz-replay
	@echo "✓ make check-deep passed"

check-platforms: test-vm-linux test-vm-windows
	@echo "✓ make check-platforms passed"

check-all:
	@s1=FAIL; s2=FAIL; s3=FAIL; \
	$(MAKE) --no-print-directory check && s1=PASS; \
	$(MAKE) --no-print-directory check-deep && s2=PASS; \
	$(MAKE) --no-print-directory check-platforms && s3=PASS; \
	echo "=== make check-all summary: $(GIT_SHA), $$(date -u +%Y-%m-%dT%H:%MZ), $$(uname -s)"; \
	printf '  %-16s %s\n' check $$s1 check-deep $$s2 check-platforms $$s3; \
	[ "$$s1$$s2$$s3" = PASSPASSPASS ]

# Kept for muscle memory and older docs: the same as `make check`.
review-check: check

release:
	@if [ -z "$(VERSION)" ]; then echo "Usage: make release VERSION=x.y.z [FLAGS=--dry-run]"; exit 1; fi
	./scripts/release.sh "$(VERSION)" $(FLAGS)

docs: build
	./cfire doc

fmt: build
	./cfire fmt examples

test: build .cfire_test_runner
	./.cfire_test_runner
	./cfire test tests/stdlib_suite_test.cf
	./cfire doc --check
	./.cfire_test_runner guide
	./.cfire_test_runner conformance

test-unit: build .cfire_test_runner
	./.cfire_test_runner unit

test-stdlib: build .cfire_test_runner
	./.cfire_test_runner stdlib
	./cfire test tests/stdlib_suite_test.cf

test-e2e: build .cfire_test_runner
	./.cfire_test_runner e2e

test-guide: build .cfire_test_runner
	./.cfire_test_runner guide

# Filter and options: ./.cfire_test_runner conformance --filter 'ownership/*' -v
test-conformance: build .cfire_test_runner
	./.cfire_test_runner conformance

ASAN_CXXFLAGS := -std=c++2b -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) -Iruntime -Isrc
ASAN_LDFLAGS  := -fsanitize=address,undefined -fno-sanitize-recover=undefined $(LDFLAGS)

test-asan: build .cfire_test_runner
	@echo "=== Building & Running All C++ Suites under AddressSanitizer & UndefinedBehaviorSanitizer ==="
	$(CXX) $(ASAN_CXXFLAGS) $(TEST_SRCS) $(ASAN_LDFLAGS) -o .cfire_test_runner_asan
	./.cfire_test_runner_asan
	rm -f .cfire_test_runner_asan
	@echo "=== Running the Conformance Corpus with Generated Programs under ASan/UBSan ==="
	./.cfire_test_runner conformance --run-flags=--sanitize

TSAN_CXXFLAGS := -std=c++2b -O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) -Iruntime -Isrc

test-tsan: build .cfire_test_runner
	@echo "=== Building & Running Runtime Concurrency Tests under ThreadSanitizer ==="
	$(CXX) $(TSAN_CXXFLAGS) tests/tsan_main.cpp -fsanitize=thread $(LDFLAGS) -o .cfire_test_runner_tsan
	./.cfire_test_runner_tsan
	rm -f .cfire_test_runner_tsan
	@echo "=== Running Conformance Concurrency Programs under ThreadSanitizer ==="
	./.cfire_test_runner conformance --filter 'concurrency/*' --run-flags=--sanitize=thread

fuzz: build .cfire_test_runner
	./.cfire_test_runner fuzz
	./cfire test tests/stdlib_suite_test.cf --fuzz-runs 500

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

# --- Local UTM VM gates (CF-184, Q48): the CI `linux` / `windows` jobs on the working tree. See scripts/vm/README.md.
# The host-side driver (CF-187) is C++ like everything else; only the guest-side gate scripts are shell.
.vmtest: tools/vmtest/main.cpp tools/vmtest/vmtest.hpp tests/process_run.hpp
	@$(CXX) $(CXXFLAGS) tools/vmtest/main.cpp $(LDFLAGS) -o $@ 2> $@.log; rc=$$?; cat $@.log >&2; echo "$(CXX) ... tools/vmtest/main.cpp (rc=$$rc)"; exit $$rc

test-vm-linux: .vmtest
	./.vmtest linux

test-vm-windows: .vmtest
	./.vmtest windows

vm-setup-linux: .vmtest
	./.vmtest setup-linux

vm-setup-windows: .vmtest
	./.vmtest setup-windows

clean:
	rm -rf cfire .cfire_gitsha .cfire_build.log .vmtest .vmtest.log $(TEST_OBJS:=.log) .cfire_test_runner .cfire_test_runner_asan .cfire_test_runner_tsan .fuzz_corpus .fuzz_crashes .fuzz_replay_* .fuzz_* $(TEST_OBJS) examples/*/.cfire tests/.cfire cfire_cross_* cfire_wasm_gui_*
