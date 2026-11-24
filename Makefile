# Shortcuts for the iot_device firmware. Every target activates ESP-IDF itself.
# Usage: make <target> [PORT=/dev/ttyACM0]

SHELL := /bin/bash
.DEFAULT_GOAL := help

PORT ?= /dev/ttyACM0
IDF_ACTIVATE ?= $(HOME)/.espressif/tools/activate_idf_v6.1.sh
IDF_ENV := source $(IDF_ACTIVATE) >/dev/null 2>&1 &&

INTEGRATION_DIR := test/integration
LINT_BUILD_DIR := build_clang
LINT_REPORT := warnings.txt
LINT_LOG := $(LINT_BUILD_DIR).log
PROJECT_DIR := $(CURDIR)

# Project C sources only (never ESP-IDF, managed components or build output).
SOURCES = $(shell find components main test -type f \( -name '*.c' -o -name '*.h' \) \
            -not -path '*/build/*' -not -path '*/managed_components/*' -not -path '*/.cache/*')

.PHONY: help build flash monitor flash-monitor test test-integration test-integration-build \
        lint format format-check check clean

help: ## List the available targets
	@grep -hE '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  %-24s %s\n", $$1, $$2}'

build: ## Build the firmware
	$(IDF_ENV) idf.py build

flash: build ## Flash the firmware to PORT
	$(IDF_ENV) idf.py -p $(PORT) flash

monitor: ## Open the serial monitor on PORT (Ctrl+] to exit)
	$(IDF_ENV) idf.py -p $(PORT) monitor

flash-monitor: build ## Flash the firmware and open the monitor
	$(IDF_ENV) idf.py -p $(PORT) flash monitor

test: ## Run every unit test project in QEMU
	$(IDF_ENV) test/run_unit_tests.sh

test-integration-build: ## Build the board-only integration tests
	$(IDF_ENV) idf.py -C $(INTEGRATION_DIR) build

test-integration: test-integration-build ## Flash the board and run the integration tests with pytest-embedded (monitor closed)
	$(IDF_ENV) pytest -c $(INTEGRATION_DIR)/pytest.ini --app-path $(INTEGRATION_DIR) --port $(PORT) \
	    $(INTEGRATION_DIR)/test_integration.py

# idf.py clang-check (https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/tools/idf-clang-tidy.html)
# with the project's .clang-tidy. It needs the clang toolchain, so it uses its own build dir. The
# PATH puts the real idf.py (the EIM shim is a shell script pyclang cannot run) and esp-clang's
# clang-tidy (understands xtensa) first.
lint: ## Static analysis (clang-tidy); fails on findings in project files
	@$(IDF_ENV) export PATH="$$IDF_PATH/tools:$$(ls -d $(HOME)/.espressif/tools/esp-clang/*/esp-clang/bin):$$PATH" && \
	IDF_TOOLCHAIN=clang python "$$IDF_PATH/tools/idf.py" -B $(LINT_BUILD_DIR) clang-check \
	    --exclude-paths managed_components --exclude-paths $(LINT_BUILD_DIR) \
	    --run-clang-tidy-options "-config-file=$(PROJECT_DIR)/.clang-tidy" >$(LINT_LOG) 2>&1 || \
	    { echo "clang-check failed, see $(LINT_LOG)"; exit 1; }
	@findings=$$(grep -E '^$(PROJECT_DIR)/(components|main)/[^:]+:[0-9]+:[0-9]+: (warning|error):' \
	    $(LINT_REPORT) | sort -u | sed 's|^$(PROJECT_DIR)/||'); \
	if [ -n "$$findings" ]; then echo "$$findings"; echo "clang-tidy: findings above (full report: $(LINT_REPORT))"; exit 1; fi; \
	echo "clang-tidy: no findings in project files"

format: ## Format the project C sources with clang-format
	clang-format -i $(SOURCES)

format-check: ## Fail if a project C source is not clang-formatted
	@clang-format --dry-run -Werror $(SOURCES) && echo "clang-format: all project sources formatted"

check: format-check lint build test ## Everything that runs without the board
	@echo "All checks passed."

clean: ## Remove build output (firmware, lint and test projects)
	rm -rf build $(LINT_BUILD_DIR) $(LINT_LOG) $(LINT_REPORT) test/*/build
