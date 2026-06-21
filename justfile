# ws — build & quality gates
# freestanding C23, no libc. clang + lld.

cc := env_var_or_default("CC", "clang")
cflags := "-std=c2x -ffreestanding -nostdlib -fno-builtin -Wall -Wextra -Werror -Wpedantic -O2 -g -Iinclude -Isrc"
testflags := "-std=c2x -Wall -Wextra -Werror -Iinclude -Isrc"   # tests link host libc for assert/printf
build_dir := "build"

default: build

_mkdir:
    @mkdir -p {{build_dir}}

# Compile the SDK as a static archive (freestanding).
build: _mkdir
    #!/usr/bin/env bash
    set -euo pipefail
    srcs=$(find src -name '*.c')
    objs=""
    for s in $srcs; do
      o="{{build_dir}}/$(echo "$s" | tr '/' '_').o"
      {{cc}} {{cflags}} -c "$s" -o "$o"
      objs="$objs $o"
    done
    ar rcs {{build_dir}}/libws.a $objs
    echo "built {{build_dir}}/libws.a"

# Unit tests: compiled against host toolchain (libc allowed in test harness only).
test: _mkdir
    #!/usr/bin/env bash
    set -euo pipefail
    fail=0
    for t in tests/unit/test_*.c; do
      bin="{{build_dir}}/$(basename "$t" .c)"
      # tests include the unit-under-test source directly (white-box).
      {{cc}} {{testflags}} "$t" -o "$bin"
      if "$bin"; then echo "PASS $(basename "$t")"; else echo "FAIL $(basename "$t")"; fail=1; fi
    done
    exit $fail

# End-to-end against a reference WebSocket peer.
e2e: build
    python3 tests/e2e/run_e2e.py

# Local performance measurement.
bench: _mkdir
    #!/usr/bin/env bash
    set -euo pipefail
    {{cc}} {{testflags}} -O3 -DNDEBUG bench/bench_frame.c -o {{build_dir}}/bench_frame
    {{build_dir}}/bench_frame

# Cyclomatic complexity gate (fail if CCN > 10).
cyclo:
    lizard src -C 10 -w

# Static analysis + format check.
lint:
    #!/usr/bin/env bash
    set -euo pipefail
    clang-format --dry-run --Werror $(find src include tests bench -name '*.c' -o -name '*.h')
    find src -name '*.c' | xargs clang-tidy --quiet -- {{cflags}}

fmt:
    find src include tests bench -name '*.c' -o -name '*.h' | xargs clang-format -i

# Formal verification (Lean 4 via dotfiles toolchain).
proof:
    cd proof && lake build

# Full CI gate.
ci: proof lint cyclo build test e2e bench
    @echo "CI OK"
