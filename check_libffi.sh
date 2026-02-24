#!/usr/bin/env bash
# Check that libffi is findable for Fusion's build.
# Run from repo root. Source env_local_deps.sh first if you use ~/.local libffi:
#   source ./env_local_deps.sh && ./check_libffi.sh

echo "=== 1. PKG_CONFIG_PATH (must include ~/.local if libffi is there) ==="
echo "PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-<unset>}"

echo ""
echo "=== 2. pkg-config libffi ==="
if pkg-config --exists libffi 2>/dev/null; then
  echo "pkg-config finds libffi: yes"
  pkg-config --modversion libffi
  pkg-config --cflags --libs libffi
else
  echo "pkg-config finds libffi: NO"
fi

echo ""
echo "=== 3. ~/.local libffi files (if you installed to ~/.local) ==="
for d in "$HOME/.local/lib" "$HOME/.local/lib64"; do
  if [ -d "$d" ]; then
    echo "  $d:"
    ls "$d"/libffi* 2>/dev/null || echo "    (no libffi*)"
    ls "$d/pkgconfig/libffi.pc" 2>/dev/null || true
  fi
done
echo "  $HOME/.local/include:"
ls "$HOME/.local/include/ffi"* 2>/dev/null || echo "    (no ffi*)"

echo ""
echo "=== 4. After a clean configure, CMakeCache.txt should have LibFFI found ==="
echo "    Run: source ./env_local_deps.sh && rm -rf build && ./test.sh"
echo "    Then: grep -E 'LibFFI_|FUSION_LIBFFI' build/CMakeCache.txt"
