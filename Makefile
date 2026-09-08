.PHONY: all build test test-unit test-integration clean coverage-build test-coverage \
	format format-check lint lint-build fuzz fuzz-build qa

NPROC := $(shell command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu 2>/dev/null || echo 2)
JOBS := $(shell expr $(NPROC) / 4)
COVERAGE_BUILD_DIR := build/coverage
LINT_BUILD_DIR := build/lint
FUZZ_BUILD_DIR := build/fuzz
PYTHON := python3
BADGE_JSON := .github/badges/coverage.json
CLANGXX ?= clang++
FUZZ_RUNS ?= 4096
FUZZ_MAX_TIME ?= 20
REQPACK_CLI_DIR ?= ../reqpack
SOL2_INCLUDE_DIR_OVERRIDE ?=

CMAKE_TEST_FLAGS := -DREQPACK_CORE_BUILD_TESTS=ON -DREQPACK_CLI_DIR=$(REQPACK_CLI_DIR)
ifneq ($(SOL2_INCLUDE_DIR_OVERRIDE),)
CMAKE_TEST_FLAGS += -DSOL2_INCLUDE_DIR_OVERRIDE=$(SOL2_INCLUDE_DIR_OVERRIDE)
endif

all: build

build:
	cmake -S . -B build $(CMAKE_TEST_FLAGS)
	cmake --build build -j$(JOBS)

test: build
	@ctest --test-dir build --output-on-failure

test-unit: build
	@ctest --test-dir build --output-on-failure -R "^core::unit::"

test-integration: build
	@ctest --test-dir build --output-on-failure -R "^core::integration::"

coverage-build:
	cmake -S . -B $(COVERAGE_BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=Debug \
		$(CMAKE_TEST_FLAGS) \
		-DREQPACK_CORE_ENABLE_COVERAGE=ON
	cmake --build $(COVERAGE_BUILD_DIR) -j$(JOBS)

test-coverage: coverage-build
	@ctest --test-dir $(COVERAGE_BUILD_DIR) --output-on-failure -R "^core::unit::"
	@ctest --test-dir $(COVERAGE_BUILD_DIR) --output-on-failure -R "^core::integration::" || true
	@$(PYTHON) tests/coverage_summary.py $(COVERAGE_BUILD_DIR) . \
		--badge-json $(BADGE_JSON) | tee coverage-summary.txt

format-check:
	@chmod +x scripts/check_clang_format.sh
	@./scripts/check_clang_format.sh

format:
	@chmod +x scripts/format_sources.sh
	@./scripts/format_sources.sh

lint-build:
	cmake -S . -B $(LINT_BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=Debug \
		$(CMAKE_TEST_FLAGS) \
		-DCMAKE_CXX_COMPILER=$(CLANGXX)
	cmake --build $(LINT_BUILD_DIR) -j$(JOBS) --target reqpack_core reqpack_core_unit_tests

lint: lint-build
	@chmod +x scripts/run_clang_tidy.sh
	@./scripts/run_clang_tidy.sh $(LINT_BUILD_DIR)

fuzz-build:
	cmake -S . -B $(FUZZ_BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		$(CMAKE_TEST_FLAGS) \
		-DREQPACK_CORE_BUILD_FUZZERS=ON \
		-DCMAKE_CXX_COMPILER=$(CLANGXX)
	cmake --build $(FUZZ_BUILD_DIR) -j$(JOBS)

fuzz: fuzz-build
	@chmod +x scripts/run_fuzzers.sh
	@FUZZ_RUNS=$(FUZZ_RUNS) FUZZ_MAX_TIME=$(FUZZ_MAX_TIME) ./scripts/run_fuzzers.sh $(FUZZ_BUILD_DIR)

qa: format-check lint test-unit fuzz

clean:
	@rm -rf build $(COVERAGE_BUILD_DIR) $(LINT_BUILD_DIR) $(FUZZ_BUILD_DIR) coverage-summary.txt
