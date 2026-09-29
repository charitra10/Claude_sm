#!/usr/bin/env bash
# mkvariant.sh SRC DEST [diag] [NAME=VALUE ...]: copy a bot's sources and rewrite `constexpr <type> NAME = ...;` lines.
# "diag" defines BOT_DIAG (stderr tracing; never for submission); "instr" also adds the audit trace points.
set -euo pipefail
src=$1; dest=$2; shift 2
rm -rf "$dest"; mkdir -p "$dest"
cp "$src"/main.cpp "$src"/helper.hpp "$src"/bot.toml "$dest"/
for kv in "$@"; do
  if [ "$kv" = diag ]; then sed -i '1i #define BOT_DIAG' "$dest/main.cpp"; continue; fi
  if [ "$kv" = instr ]; then python3 "$(dirname "$0")/instrument.py" "$dest/main.cpp"; sed -i '1i #define BOT_DIAG' "$dest/main.cpp"; continue; fi
  k=${kv%%=*}; v=${kv#*=}
  n=$(grep -cE "^constexpr (bool|int|double) $k = " "$dest/main.cpp" || true)
  [ "$n" = 1 ] || { echo "flag $k matched $n lines" >&2; exit 1; }
  sed -i -E "s/^(constexpr (bool|int|double) $k = )[^;]*;/\1$v;/" "$dest/main.cpp"
done
