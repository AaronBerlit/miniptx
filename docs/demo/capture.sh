#!/usr/bin/env bash
# Runs the demo commands for real and records them in demo.txt, which
# render_gif.py turns into docs/demo.gif. Run from the repo root after building:
#   bash docs/demo/capture.sh
set -e
out=docs/demo/demo.txt
: > "$out"
scene() {  # each scene starts on a cleared screen
  [ -s "$out" ] && echo "---" >> "$out"
  for cmd in "$@"; do
    echo "\$ $cmd" >> "$out"
    eval "$cmd" >> "$out" 2>&1
  done
}
export PATH="$PWD/build:$PATH"

scene "grep -v '^// [RE]' examples/saxpy.tiny"
scene "miniptx examples/saxpy.tiny"
scene "miniptx examples/saxpy.tiny --threads 5 --run saxpy 2.0 [1,2,3,4] [10,10,10,10] 4" \
      "miniptx --stats examples/is_prime.tiny" \
      "./build/unit_tests | tail -1"
echo "wrote $out"
