# ws — build & quality gates
# freestanding C23, no libc. clang + lld.

cc := env_var_or_default("CC", "clang")
cflags := "-std=c2x -ffreestanding -nostdlib -fno-builtin -Wall -Wextra -Werror -Wpedantic -O2 -g -Iinclude -Isrc"
testflags := "-std=c2x -Wall -Wextra -Werror -Iinclude -Isrc"   # tests link host libc for assert/printf
build_dir := "build"

default: build

_mkdir:
    @mkdir -p {{build_dir}}

# Compile the SDK static archive (freestanding) + the demo server binary.
# server.c provides _start and is the entry point, so it stays out of libws.a.
build: _mkdir
    #!/usr/bin/env bash
    set -euo pipefail
    libobjs=""
    for s in $(find src -name '*.c' ! -name 'server.c'); do
      o="{{build_dir}}/$(echo "$s" | tr '/' '_').o"
      {{cc}} {{cflags}} -c "$s" -o "$o"
      libobjs="$libobjs $o"
    done
    ar rcs {{build_dir}}/libws.a $libobjs
    {{cc}} {{cflags}} -c src/sdk/server.c -o {{build_dir}}/server.o
    {{cc}} -nostdlib -static -fuse-ld=lld -o {{build_dir}}/ws_server {{build_dir}}/server.o {{build_dir}}/libws.a
    echo "built {{build_dir}}/libws.a and {{build_dir}}/ws_server"

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

# Build the libc-hosted echo example (links the freestanding libws.a).
# Uses ordinary sockets, so it compiles against host libc unlike the demo server.
example: build
    {{cc}} {{testflags}} examples/echo/echo.c {{build_dir}}/libws.a -o {{build_dir}}/echo
    @echo "built {{build_dir}}/echo — run it, then connect to ws://127.0.0.1:9002"

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
    mapfile -t allsrc < <(find src include tests bench \( -name '*.c' -o -name '*.h' \))
    clang-format --dry-run --Werror "${allsrc[@]}"
    mapfile -t csrc < <(find src -name '*.c')
    # source files BEFORE `--`; compiler flags AFTER.
    clang-tidy --quiet "${csrc[@]}" -- {{cflags}}

fmt:
    #!/usr/bin/env bash
    set -euo pipefail
    mapfile -t allsrc < <(find src include tests bench \( -name '*.c' -o -name '*.h' \))
    clang-format -i "${allsrc[@]}"

# Formal verification (Lean 4 via dotfiles toolchain).
proof:
    cd proof && lake build

# Full CI gate.
ci: proof lint cyclo build test e2e bench
    @echo "CI OK"
