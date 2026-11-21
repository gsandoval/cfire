CXX      := clang++
JOBS     ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MAKE_J   := $(if $(filter -j% --jobserver%,$(MAKEFLAGS)),,-j$(JOBS))
MEM_GB       := $(shell sh -c 'b=$$(sysctl -n hw.memsize 2>/dev/null) && [ -n "$$b" ] && { echo $$(( b / 1073741824 )); exit 0; }; kb=$$(awk "/^MemTotal:/ {print \$$2}" /proc/meminfo 2>/dev/null) && [ -n "$$kb" ] && { echo $$(( kb / 1048576 )); exit 0; }; echo 8')
SAN_JOBS     ?= $(shell sh -c 'm=$(MEM_GB); j=$(JOBS); s=$$(( m / 3 )); [ "$$s" -lt 1 ] && s=1; [ "$$s" -gt "$$j" ] && s="$$j"; echo "$$s"')
SAN_RUN_JOBS ?= $(shell sh -c 'm=$(MEM_GB); j=$(JOBS); s=$$(( m - 2 )); [ "$$s" -lt 2 ] && s=2; [ "$$s" -gt "$$j" ] && s="$$j"; echo "$$s"')
CFIRE_VER := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || sed -n 's/.*kVersion = "\([^"]*\)".*/\1/p' src/version.hpp 2>/dev/null || echo 0.0.0-dev)
GIT_SHA  ?= $(shell sh -c 'sha=$$(git rev-parse --short=7 HEAD 2>/dev/null) || { echo dev; exit 0; }; git diff --quiet --ignore-submodules HEAD 2>/dev/null && printf "%s" "$$sha" || printf "%s-dirty" "$$sha"')
UNAME_S  := $(shell uname -s)
IS_WIN   := $(or $(findstring MINGW,$(UNAME_S)),$(findstring MSYS,$(UNAME_S)),$(findstring CYGWIN,$(UNAME_S)),$(findstring CLANG,$(UNAME_S)),$(findstring CLANG,$(MSYSTEM)),$(findstring Windows_NT,$(OS)))
EXE      := $(if $(IS_WIN),.exe,)
LINUX_CXXFLAGS := $(if $(filter Linux,$(UNAME_S)),-Wno-builtin-macro-redefined -D__cpp_concepts=202002L,)
PCH_INSTANTIATE := $(if $(filter Linux,$(UNAME_S)),,-fpch-instantiate-templates)
WIN_CXXFLAGS := $(if $(IS_WIN),-static,)
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS ?=
CXXFLAGS := -std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) $(WIN_CXXFLAGS) -Iruntime -Isrc $(EXTRA_CXXFLAGS)
LDFLAGS  := $(if $(filter Darwin,$(UNAME_S)),-framework AudioToolbox -framework CoreFoundation -framework Security -lobjc,$(if $(IS_WIN),-static -lws2_32 -ladvapi32 -lshell32 -lole32 -luuid -lgdi32,-ldl -lpthread)) $(EXTRA_LDFLAGS)
CFIRE_LDFLAGS := $(if $(filter Darwin,$(UNAME_S)),,$(if $(IS_WIN),-static -lws2_32 -ladvapi32 -lshell32,-ldl -lpthread)) $(EXTRA_LDFLAGS)
PREFIX   ?= /usr/local

CFIRE_SRCS := src/codegen/body_emitter.cpp \
              src/codegen/declaration_emitter.cpp \
              src/sema/statement_checker.cpp \
              src/parser.cpp \
              src/lsp/server_command.cpp \
              src/lsp/language_features.cpp \
              src/build/driver.cpp \
              src/build/toolchain_and_cache.cpp \
              src/docgen/command.cpp \
              src/sema/expression_checker.cpp \
              src/sema/type_inference.cpp \
              src/sema/call_and_match_checker.cpp \
              src/lsp/navigation_and_refactor.cpp \
              src/cli/inspect_and_fmt_commands.cpp \
              src/cli/mcp_and_ide_commands.cpp \
              src/cli/build_commands.cpp \
              src/cli/check_commands.cpp \
              src/cli/project_commands.cpp \
              src/cli/toolchain_commands.cpp \
              src/diagnostics_registry.cpp \
              src/docgen/markdown_renderer.cpp \
              src/docgen/stdlib_catalog.cpp \
              src/main.cpp

CFIRE_OBJS := $(CFIRE_SRCS:.cpp=.o)

TEST_SRCS := tests/unit/test_frontend.cpp \
             tests/unit/test_sema.cpp \
             tests/unit/test_stdlib.cpp \
             tests/e2e/test_canonical.cpp \
             tests/test_main.cpp

TEST_OBJS := $(TEST_SRCS:.cpp=.o)
TEST_ASAN_OBJS := $(TEST_SRCS:.cpp=.asan.o)

.PHONY: FORCE all build hooks install uninstall doctor setup check check-fast check-warnings check-deep check-platforms check-all vm-setup-linux vm-setup-windows test test-unit test-stdlib test-e2e test-guide test-conformance test-asan test-tsan test-vm-linux test-vm-windows fuzz fuzz-replay fuzz-lib bench docs fmt review-check release clean

all: build

HEADERS := $(wildcard src/*.hpp src/*/*.hpp runtime/*.hpp runtime/*/*.hpp runtime/*/*/*.hpp tests/*.hpp tests/unit/*.hpp tools/*/*.hpp)

# `cfire` is a real file target: it relinks only when sources change or the embedded git SHA does.
.cfire_gitsha: FORCE
	@echo '$(GIT_SHA)' | cmp -s - $@ 2>/dev/null || echo '$(GIT_SHA)' > $@

.cfire_host.pch: src/host_pch.hpp .cfire_gitsha
	@$(CXX) $(CXXFLAGS) -DCFIRE_SPLIT_TUS=1 $(PCH_INSTANTIATE) -MMD -MP -MF .cfire_host.pch.d -x c++-header src/host_pch.hpp -o $@ 2> $@.log; rc=$$?; cat $@.log >&2; echo "$(CXX) ... -x c++-header src/host_pch.hpp (rc=$$rc)"; exit $$rc

$(CFIRE_OBJS): %.o: %.cpp .cfire_host.pch .cfire_gitsha
	@$(CXX) $(CXXFLAGS) -DCFIRE_SPLIT_TUS=1 -include-pch .cfire_host.pch -MMD -MP -c $< -o $@ 2> $@.log; rc=$$?; cat $@.log >&2; echo "$(CXX) ... -c $< (rc=$$rc)"; exit $$rc

# Each compile keeps its diagnostics in a .log next to its output, so `make check` can prove zero warnings even when
# nothing needed rebuilding.
cfire$(EXE): $(CFIRE_OBJS)
	@$(CXX) $(CXXFLAGS) $(CFIRE_OBJS) $(CFIRE_LDFLAGS) -o cfire$(EXE) 2> .cfire_build.log; rc=$$?; cat .cfire_build.log >&2; echo "$(CXX) ... -o cfire$(EXE) (rc=$$rc)"; exit $$rc

build: cfire$(EXE)

FORCE:

hooks:
	@git config core.hooksPath .githooks

install: build
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/cfire
	cp cfire$(EXE) $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$ && chmod 755 $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$ && mv -f $(DESTDIR)$(PREFIX)/bin/.cfire.tmp.$$$$ $(DESTDIR)$(PREFIX)/bin/cfire$(EXE)
	@set -e; \
	share="$(DESTDIR)$(PREFIX)/share/cfire"; \
	stage="$${share}/.runtime.tmp.$$$$"; \
	old="$${share}/.runtime.old.$$$$"; \
	rm -rf "$$stage" "$$old"; \
	mkdir -p "$$stage"; \
	cp -R runtime/. "$$stage/"; \
	rm -f "$$stage/VERSION"; \
	printf "%s %s\n" "$(CFIRE_VER)" "$(GIT_SHA)" > "$$stage/cfire-runtime.version"; \
	if [ -d "$${share}/runtime" ]; then mv "$${share}/runtime" "$$old"; fi; \
	mv "$$stage" "$${share}/runtime"; \
	rm -rf "$$old"
	@echo "✓ Installed cfire $(CFIRE_VER) ($(GIT_SHA)) to $(DESTDIR)$(PREFIX)/bin/cfire$(EXE) and runtime to $(DESTDIR)$(PREFIX)/share/cfire"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cfire$(EXE)
	rm -rf $(DESTDIR)$(PREFIX)/share/cfire

doctor: build
	./cfire doctor

setup: build
	./cfire setup

.cfire_test.pch: tests/test_pch.hpp .cfire_gitsha
	@$(CXX) $(CXXFLAGS) $(PCH_INSTANTIATE) -MMD -MP -MF .cfire_test.pch.d -x c++-header tests/test_pch.hpp -o $@ 2> $@.log; rc=$$?; cat $@.log >&2; echo "$(CXX) ... -x c++-header tests/test_pch.hpp (rc=$$rc)"; exit $$rc

$(TEST_OBJS): %.o: %.cpp .cfire_test.pch .cfire_gitsha
	@$(CXX) $(CXXFLAGS) -include-pch .cfire_test.pch -MMD -MP -c $< -o $@ 2> $@.log; rc=$$?; cat $@.log >&2; echo "$(CXX) ... -c $< (rc=$$rc)"; exit $$rc

.cfire_test_runner$(EXE): $(TEST_OBJS)
	@$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(LDFLAGS) -o $@ 2> .cfire_test_runner.log; rc=$$?; cat .cfire_test_runner.log >&2; echo "$(CXX) ... -o $@ (rc=$$rc)"; exit $$rc

ifneq ($(EXE),)
.PHONY: .cfire_test_runner
.cfire_test_runner: .cfire_test_runner$(EXE)
endif

%.o: %.cpp $(HEADERS)
	@$(CXX) $(CXXFLAGS) -c $< -o $@ 2> $@.log; rc=$$?; cat $@.log >&2; echo "$(CXX) ... -c $< (rc=$$rc)"; exit $$rc

# --- The three test tiers (plus check-fast for sandbox-safe inner-loop compiler checks). See docs/planning/README.md:
#   make check-fast       sandbox-safe compiler/Sema/fmt/doc checks (no loopback TCP suites)
#   make check            every commit (the pre-commit hook runs it)
#   make check-deep       sanitizers and fuzz replay; required before a CF item is marked done
#   make check-platforms  the Linux and Windows VMs in parallel; required before a CF item is marked done, and before a release
#   make check-all        all three in order, then a summary table; its summary goes in active.md
check-fast:
	@[ -f .cfire_build.log ] || rm -f cfire cfire.exe
	@[ -f .cfire_host.pch.log ] || rm -f .cfire_host.pch
	@[ -f .cfire_test.pch.log ] || rm -f .cfire_test.pch
	@[ -f .cfire_test_runner.log ] || rm -f .cfire_test_runner .cfire_test_runner.exe
	@for o in $(CFIRE_OBJS) $(TEST_OBJS); do [ -f $$o.log ] || rm -f $$o; done
	@$(MAKE) $(MAKE_J) --no-print-directory build .cfire_test_runner$(EXE)
	@$(MAKE) --no-print-directory check-warnings
	@echo "=== Canonical formatting"
	./cfire$(EXE) fmt --check examples/
	@echo "=== Documentation sync"
	./cfire$(EXE) doc --check
	./.cfire_test_runner$(EXE) fast
	@echo "✓ make check-fast passed"

check:
	@[ -f .cfire_build.log ] || rm -f cfire cfire.exe
	@[ -f .cfire_host.pch.log ] || rm -f .cfire_host.pch
	@[ -f .cfire_test.pch.log ] || rm -f .cfire_test.pch
	@[ -f .cfire_test_runner.log ] || rm -f .cfire_test_runner .cfire_test_runner.exe
	@for o in $(CFIRE_OBJS) $(TEST_OBJS); do [ -f $$o.log ] || rm -f $$o; done
	@$(MAKE) $(MAKE_J) --no-print-directory build .cfire_test_runner$(EXE)
	@$(MAKE) --no-print-directory check-warnings
	@echo "=== Canonical formatting"
	./cfire$(EXE) fmt --check examples/
	@echo "=== Every canonical example type-checks"
	@set -e; pids=""; \
	for d in examples/*/; do \
		b=$$(basename "$$d"); \
		( ./cfire$(EXE) check --verify-cpp "$$d" > ".cfire_check_$$b.log" 2>&1 || exit 1; \
		  if grep -q "cfire/UNKNOWN-TYPE" ".cfire_check_$$b.log"; then exit 2; fi ) & \
		pids="$$pids $$!:$$b:$$d"; \
	done; \
	fail=0; \
	for item in $$pids; do \
		pid=$${item%%:*}; rest=$${item#*:}; b=$${rest%%:*}; d=$${rest#*:}; \
		if ! wait "$$pid"; then \
			cat ".cfire_check_$$b.log"; \
			if grep -q "cfire/UNKNOWN-TYPE" ".cfire_check_$$b.log"; then echo "untyped expression in $$d"; else echo "cfire check failed: $$d"; fi; \
			fail=1; \
		fi; \
		rm -f ".cfire_check_$$b.log"; \
	done; \
	[ $$fail -eq 0 ]
	@$(MAKE) --no-print-directory test
	@echo "✓ make check passed"

check-warnings:
	@n=$$(cat .cfire_host.pch.log .cfire_test.pch.log .cfire_build.log .cfire_test_runner.log $(CFIRE_OBJS:=.log) $(TEST_OBJS:=.log) $(wildcard .vmtest.log) 2>/dev/null | grep -c 'warning:'); \
	if [ "$$n" != "0" ]; then cat .cfire_host.pch.log .cfire_test.pch.log .cfire_build.log .cfire_test_runner.log $(CFIRE_OBJS:=.log) $(TEST_OBJS:=.log) $(wildcard .vmtest.log) 2>/dev/null | grep -A4 'warning:'; echo "✗ $$n C++ compiler warning(s); the gate requires zero"; exit 1; fi; \
	echo "✓ zero C++ compiler warnings"

check-deep:
	@$(MAKE) --no-print-directory test-asan
	@$(MAKE) --no-print-directory test-tsan
	@$(MAKE) --no-print-directory fuzz-replay
	@echo "✓ make check-deep passed"

check-platforms: .vmtest
	./.vmtest both
	@echo "✓ make check-platforms passed"

check-all:
	@s1=FAIL; s2=FAIL; s3=FAIL; \
	$(MAKE) $(MAKE_J) --no-print-directory check && s1=PASS; \
	$(MAKE) --no-print-directory check-deep && s2=PASS; \
	$(MAKE) --no-print-directory check-platforms && s3=PASS; \
	echo "=== make check-all summary: $(GIT_SHA), $$(date -u +%Y-%m-%dT%H:%MZ), $$(uname -s)"; \
	printf '  %-16s %s\n' check $$s1 check-deep $$s2 check-platforms $$s3; \
	[ "$$s1$$s2$$s3" = PASSPASSPASS ]

# Kept for muscle memory and older docs: the same as `make check`.
review-check: check

release:
	@target="$(if $(VERSION),$(VERSION),$(BUMP))"; \
	if [ -z "$$target" ]; then echo "Usage: make release BUMP=patch|minor|major [FLAGS=--dry-run] (or VERSION=x.y.z)"; exit 1; fi; \
	./scripts/release.sh "$$target" $(FLAGS)

docs: build
	./cfire$(EXE) doc

fmt: build
	./cfire$(EXE) fmt examples

test: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE)
	./cfire$(EXE) test tests/stdlib_suite_test.cf
	./cfire$(EXE) doc --check
	./.cfire_test_runner$(EXE) guide
	./.cfire_test_runner$(EXE) conformance

test-unit: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE) unit

test-stdlib: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE) stdlib
	./cfire$(EXE) test tests/stdlib_suite_test.cf

test-e2e: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE) e2e

test-guide: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE) guide

# Filter and options: ./.cfire_test_runner conformance --filter 'ownership/*' -v
test-conformance: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE) conformance

ASAN_CXXFLAGS := -std=c++2b -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) -Iruntime -Isrc
ASAN_LDFLAGS  := -fsanitize=address,undefined -fno-sanitize-recover=undefined $(LDFLAGS)

.cfire_test_asan.pch: tests/test_pch.hpp .cfire_gitsha
	$(CXX) $(ASAN_CXXFLAGS) $(PCH_INSTANTIATE) -MMD -MP -MF .cfire_test_asan.pch.d -x c++-header tests/test_pch.hpp -o $@

$(TEST_ASAN_OBJS): %.asan.o: %.cpp .cfire_test_asan.pch .cfire_gitsha
	$(CXX) $(ASAN_CXXFLAGS) -include-pch .cfire_test_asan.pch -MMD -MP -c $< -o $@

.cfire_test_runner_asan$(EXE): $(TEST_ASAN_OBJS)
	$(CXX) $(ASAN_CXXFLAGS) $(TEST_ASAN_OBJS) $(ASAN_LDFLAGS) -o $@

test-asan:
	@echo "=== Building & Running All C++ Suites under AddressSanitizer & UndefinedBehaviorSanitizer ==="
	@$(MAKE) $(MAKE_J) --no-print-directory build .cfire_test_runner$(EXE)
	@MAKEFLAGS= $(MAKE) -j$(SAN_JOBS) --no-print-directory .cfire_test_runner_asan$(EXE)
	./.cfire_test_runner_asan$(EXE) --jobs $(SAN_RUN_JOBS)
	@echo "=== Running the Conformance Corpus with Generated Programs under ASan/UBSan ==="
	./.cfire_test_runner$(EXE) conformance --jobs $(SAN_RUN_JOBS) --run-flags=--sanitize

TSAN_CXXFLAGS := -std=c++2b -O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wall -Wextra -Wno-parentheses-equality -DCFIRE_GIT_SHA=\"$(GIT_SHA)\" $(LINUX_CXXFLAGS) -Iruntime -Isrc

.cfire_test_runner_tsan$(EXE): tests/tsan_main.cpp .cfire_gitsha
	$(CXX) $(TSAN_CXXFLAGS) -MMD -MP -MF .cfire_tsan.d tests/tsan_main.cpp -fsanitize=thread $(LDFLAGS) -o $@

test-tsan:
	@echo "=== Building & Running Runtime Concurrency Tests under ThreadSanitizer ==="
	@$(MAKE) $(MAKE_J) --no-print-directory build .cfire_test_runner$(EXE) .cfire_test_runner_tsan$(EXE)
	./.cfire_test_runner_tsan$(EXE)
	@echo "=== Running Conformance Concurrency Programs under ThreadSanitizer ==="
	./.cfire_test_runner$(EXE) conformance --filter 'concurrency/*' --run-flags=--sanitize=thread

fuzz: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE) fuzz
	./cfire$(EXE) test tests/stdlib_suite_test.cf --fuzz-runs 500

# --- Structural fuzz targets (tests/fuzz/fuzz_*.cpp) ---
FUZZ_TARGETS   := parser sema fmt http url websocket json_rpc deflate
FUZZ_REPLAY_BINS := $(addprefix .fuzz_replay_,$(FUZZ_TARGETS))
FUZZ_CF_SEEDS  := examples tests/golden tests/conformance tests/parse_fail scripts/audit/probes tests/fuzz/corpus/sema_crashes
FUZZ_SECONDS   ?= 60
FUZZ_CXX       ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++ /usr/lib/llvm-*/bin/clang++) clang++)

.fuzz_replay_%: tests/fuzz/fuzz_%.cpp tests/fuzz/replay_main.cpp .cfire_gitsha
	$(CXX) $(ASAN_CXXFLAGS) -MMD -MP -MF $@.d tests/fuzz/fuzz_$*.cpp tests/fuzz/replay_main.cpp $(ASAN_LDFLAGS) -o $@

# Replays every committed seed through every target under ASan+UBSan; works without libFuzzer.
fuzz-replay:
	@MAKEFLAGS= $(MAKE) -j$(SAN_JOBS) --no-print-directory $(FUZZ_REPLAY_BINS)
	@set -e; for t in $(FUZZ_TARGETS); do \
		echo "=== fuzz-replay $$t"; \
		case $$t in parser|sema|fmt) seeds="$(FUZZ_CF_SEEDS)";; *) seeds="tests/fuzz/corpus/$$t";; esac; \
		./.fuzz_replay_$$t $$seeds; \
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

bench: build .cfire_test_runner$(EXE)
	./.cfire_test_runner$(EXE) bench
	./cfire$(EXE) bench tests/stdlib_suite_test.cf

# --- Local UTM VM gates: the CI `linux` / `windows` jobs on the working tree. See scripts/vm/README.md.
# The host-side driver is C++ like everything else; only the guest-side gate scripts are shell.
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
	@if [ -x ./cfire$(EXE) ] && ./cfire$(EXE) --version >/dev/null 2>&1; then \
		for m in examples/*/build.cf; do if [ -f "$$m" ]; then ./cfire$(EXE) clean "$$m"; fi; done; \
		./cfire$(EXE) clean examples; \
		./cfire$(EXE) clean tests; \
		./cfire$(EXE) clean --cache; \
	fi
	rm -rf cfire cfire.exe .cfire_gitsha .cfire_build.log .cfire_host.pch .cfire_host.pch.log .cfire_host.pch.d .cfire_test.pch .cfire_test.pch.log .cfire_test.pch.d .cfire_test_asan.pch .cfire_test_asan.pch.d $(CFIRE_OBJS) $(CFIRE_OBJS:=.log) $(CFIRE_OBJS:.o=.d) .vmtest .vmtest.log $(TEST_OBJS:=.log) .cfire_test_runner.log .cfire_test_runner .cfire_test_runner.exe .cfire_test_runner_asan .cfire_test_runner_asan.exe .cfire_test_runner_tsan .cfire_test_runner_tsan.exe .cfire_tsan.d .fuzz_corpus .fuzz_crashes .fuzz_replay_* .fuzz_* $(TEST_OBJS) $(TEST_OBJS:.o=.d) $(TEST_ASAN_OBJS) $(TEST_ASAN_OBJS:.asan.o=.asan.d) tests/unit/*.o.tmp cfire_cross_* cfire_wasm_gui_* .cfire_check*.log

-include .cfire_host.pch.d .cfire_test.pch.d .cfire_test_asan.pch.d .cfire_tsan.d $(CFIRE_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(TEST_ASAN_OBJS:.asan.o=.asan.d) $(FUZZ_REPLAY_BINS:=.d)
