#!/usr/bin/env bash
# Builds and runs the framing and dispatch fuzz target.
#
# libFuzzer ships with upstream Clang and not with Apple Clang, so on macOS
# this uses the Homebrew LLVM toolchain. The target is built with
# AddressSanitizer and UndefinedBehaviorSanitizer, because a fuzzer without a
# sanitizer only finds inputs that crash outright and misses the out-of-bounds
# read this target exists to look for.
#
#   usage: tools/fuzz.sh [seconds]      default 600
#
# Record every campaign in docs/correctness.md: date, duration, executions,
# corpus size, and any input added to the corpus.
set -euo pipefail

SECONDS_TO_RUN="${1:-600}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${FUZZ_OUT:-$ROOT/build/fuzz}"
CORPUS="${FUZZ_CORPUS:-$OUT/corpus}"

# Pick a Clang that has libFuzzer.
if [ -n "${CXX:-}" ]; then
  FUZZ_CXX="$CXX"
elif [ -x /opt/homebrew/opt/llvm/bin/clang++ ]; then
  FUZZ_CXX=/opt/homebrew/opt/llvm/bin/clang++
elif [ -x /usr/local/opt/llvm/bin/clang++ ]; then
  FUZZ_CXX=/usr/local/opt/llvm/bin/clang++
else
  FUZZ_CXX=clang++
fi

if ! printf 'extern "C" int LLVMFuzzerTestOneInput(const unsigned char*, unsigned long){return 0;}\n' \
    | "$FUZZ_CXX" -x c++ -std=c++20 -fsanitize=fuzzer - -o /dev/null 2> /dev/null; then
  echo "$FUZZ_CXX does not provide libFuzzer." >&2
  echo "On macOS: brew install llvm, then re-run." >&2
  exit 2
fi

mkdir -p "$OUT" "$CORPUS"

echo "compiler  $("$FUZZ_CXX" --version | head -1)"
"$FUZZ_CXX" -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
  -I"$ROOT/include" -g -O1 \
  -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
  -o "$OUT/fuzz_frame" "$ROOT/tests/fuzz_frame.cpp"

# Seed the corpus with well-formed sessions, once. Starting from valid frames
# lets the fuzzer spend its budget mutating toward the malformed cases rather
# than rediscovering the framing.
if [ -z "$(ls -A "$CORPUS" 2> /dev/null)" ]; then
  echo "seeding corpus ..."
  "$ROOT/tools/gen_fuzz_corpus.sh" "$CORPUS"
fi

echo "corpus    $(ls -1 "$CORPUS" | wc -l | tr -d ' ') inputs"
echo "duration  ${SECONDS_TO_RUN}s"
echo

"$OUT/fuzz_frame" "$CORPUS" \
  -max_total_time="$SECONDS_TO_RUN" \
  -print_final_stats=1 \
  -artifact_prefix="$OUT/"
