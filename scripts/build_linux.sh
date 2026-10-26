#!/usr/bin/env bash
set -euo pipefail

CXX="${CXX:-clang++}"
CC="${CC:-clang}"
OUT="${OUT:-build-linux}"
STATIC="${YOYO_STATIC:-1}"

mkdir -p "$OUT/bin/plugins" "$OUT/qjs"

echo "[build-linux] pwd: $(pwd)"
echo "[build-linux] files: $(ls src/runtime/*.cpp 2>/dev/null | wc -l) runtime sources"
echo "[build-linux] compiler: $("$CXX" --version 2>&1 | head -1)"
echo "[build-linux] c compiler: $("$CC" --version | head -1)"

STATIC_FLAGS=()
if [[ "$STATIC" == "1" ]]; then
    STATIC_FLAGS=(-static-libstdc++ -static-libgcc)
fi

echo "[build-linux] QuickJS"
for f in quickjs dtoa libregexp libunicode; do
    "$CC" -std=c11 -O2 -fPIC -D_GNU_SOURCE -Ithird_party/quickjs \
        -c "third_party/quickjs/$f.c" -o "$OUT/qjs/$f.o"
done

echo "[build-linux] libyoyo_runtime.so"
"$CXX" -std=c++20 -O2 -fPIC -fvisibility=hidden \
    -Iinclude -Isrc/runtime -Ithird_party/quickjs \
    -shared src/runtime/*.cpp src/runtime/js/*.cpp src/runtime/nbt/*.cpp "$OUT"/qjs/*.o \
    -o "$OUT/bin/libyoyo_runtime.so" \
    -ldl -pthread -lm "${STATIC_FLAGS[@]}"

echo "[build-linux] yoyo"
"$CXX" -std=c++20 -O2 -Iinclude -Isrc/launcher \
    src/launcher/main.cpp src/launcher/linux_exec.cpp \
    -o "$OUT/bin/yoyo" "${STATIC_FLAGS[@]}"

echo "[build-linux] sample_cpp.so"
"$CXX" -std=c++20 -O2 -fPIC -shared -Iinclude \
    plugins/sample_cpp/plugin.cpp \
    -o "$OUT/bin/plugins/sample_cpp.so"

echo "[build-linux] done"
