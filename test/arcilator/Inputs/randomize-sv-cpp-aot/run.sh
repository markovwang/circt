#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$repo_root/build-bitwuzla}"
OUT_DIR="${OUT_DIR:-$repo_root/build-bitwuzla/randomize-sv-cpp-aot-demo}"
CXX="${CXX:-clang++}"
PYTHON="${PYTHON:-python3}"

circt_verilog="$BUILD_DIR/bin/circt-verilog"
circt_opt="$BUILD_DIR/bin/circt-opt"
arcilator="$BUILD_DIR/bin/arcilator"
arc_runtime_lib="${CIRCT_ARC_RUNTIME_LIB:-$BUILD_DIR/lib/libCIRCTArcRuntime.a}"
lit_site="$BUILD_DIR/tools/circt/test/lit.site.cfg.py"

for tool in "$circt_verilog" "$circt_opt" "$arcilator"; do
  if [[ ! -x "$tool" ]]; then
    echo "missing tool: $tool" >&2
    exit 1
  fi
done

if [[ ! -f "$arc_runtime_lib" ]]; then
  echo "missing Arc runtime library: $arc_runtime_lib" >&2
  exit 1
fi

if [[ -z "${BITWUZLA_RANDOMIZE_LINK_FLAGS:-}" ]]; then
  if [[ ! -f "$lit_site" ]]; then
    echo "missing lit site config for Bitwuzla link flags: $lit_site" >&2
    exit 1
  fi
  BITWUZLA_RANDOMIZE_LINK_FLAGS="$(
    sed -n 's/^config\.bitwuzla_randomize_link_flags = "\(.*\)"$/\1/p' \
      "$lit_site"
  )"
fi

if [[ -z "$BITWUZLA_RANDOMIZE_LINK_FLAGS" ]]; then
  echo "empty Bitwuzla link flags; configure with CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE=ON" >&2
  exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

"$circt_verilog" --ir-moore "$script_dir/packet.sv" \
  -o "$OUT_DIR/randomize-moore.mlir"

"$circt_opt" --convert-moore-to-core "$OUT_DIR/randomize-moore.mlir" \
  -o "$OUT_DIR/randomize-core.mlir"

state_json="$OUT_DIR/randomize-state.json"
class_json="$OUT_DIR/randomize-classes.json"
header="$OUT_DIR/randomize-arc.h"

"$arcilator" "$OUT_DIR/randomize-core.mlir" --emit-llvm --no-runtime \
  --state-file "$state_json" --class-info-file "$class_json" \
  -o "$OUT_DIR/randomize.ll"

"$PYTHON" "$repo_root/tools/arcilator/arcilator-header-cpp.py" \
  "$state_json" --class-info "$class_json" > "$header"

# shellcheck disable=SC2086
"$CXX" "$OUT_DIR/randomize.ll" "$script_dir/testbench.cpp" "$arc_runtime_lib" \
  $BITWUZLA_RANDOMIZE_LINK_FLAGS -I"$OUT_DIR" -I"$BUILD_DIR/tools/arcilator" \
  -I"$repo_root/tools/arcilator" \
  -Wno-override-module -fuse-ld=lld \
  -o "$OUT_DIR/randomize.exe"

"$OUT_DIR/randomize.exe"
echo "randomize demo executable passed: $OUT_DIR/randomize.exe"
