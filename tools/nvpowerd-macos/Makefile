# SPDX-License-Identifier: MIT
CC = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic
CPPFLAGS = -Isrc
BUILD = build
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
PLATFORM = -framework IOKit -framework CoreFoundation
CPU_PLATFORM = -framework CoreFoundation
ARCH = -arch x86_64
endif
.PHONY: all test clean
all: $(BUILD)/nvpowerd-macos $(BUILD)/nvpowerd-cpu-probe
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/nvpowerd-macos: src/core.c src/regulator.c src/cpu_source.c src/cpu_source.h src/macos.c src/nvpowerd.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH) src/core.c src/regulator.c src/cpu_source.c src/macos.c $(PLATFORM) -o $@
$(BUILD)/test-core: src/core.c src/regulator.c tests/test_core.c src/nvpowerd.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH) src/core.c src/regulator.c tests/test_core.c -o $@
$(BUILD)/test-apple-adapter: src/core.c src/regulator.c src/cpu_source.c src/cpu_source.h src/macos.c src/nvpowerd.h tests/test_apple_adapter.c tests/apple_api_mock.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) $(ARCH) -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE=1 src/core.c src/regulator.c src/cpu_source.c tests/test_apple_adapter.c $(CPU_PLATFORM) -o $@
$(BUILD)/nvpowerd-cpu-probe: src/cpu_probe.c src/cpu_source.c src/cpu_source.h src/nvpowerd.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH) -D_POSIX_C_SOURCE=200809L src/cpu_source.c src/cpu_probe.c $(CPU_PLATFORM) -o $@
$(BUILD)/test-regulator: src/core.c src/regulator.c src/cpu_source.c tests/test_regulator.c src/nvpowerd.h src/cpu_source.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH) src/core.c src/regulator.c src/cpu_source.c tests/test_regulator.c $(CPU_PLATFORM) -o $@
$(BUILD)/test-cpu-source: src/cpu_source.c src/cpu_source.h src/nvpowerd.h tests/test_cpu_source.c tests/cpu_cf_mock.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) $(ARCH) -D_POSIX_C_SOURCE=200809L tests/test_cpu_source.c -o $@
test: $(BUILD)/test-core $(BUILD)/test-apple-adapter $(BUILD)/test-regulator $(BUILD)/test-cpu-source
	./$(BUILD)/test-core tests/fixtures
	./$(BUILD)/test-apple-adapter
	./$(BUILD)/test-regulator tests/fixtures
	./$(BUILD)/test-cpu-source
clean:
	rm -rf $(BUILD)
