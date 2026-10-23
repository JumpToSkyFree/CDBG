#!/usr/bin/env bash
# setup-deps-fedora.sh
# Installs cdbg's build dependencies on Fedora.
# Run with: chmod +x setup-deps-fedora.sh && ./setup-deps-fedora.sh

set -euo pipefail

echo "==> Installing base toolchain and packaged dependencies..."
# --allowerasing/--skip-unavailable aren't what we need here — the actual
# problem under dnf5 on Fedora 44 is that a transaction where every package
# is already installed gets reported as a resolution failure instead of a
# no-op. -y alone doesn't fix that, so we swallow that specific case per
# package to keep this script idempotent and re-runnable.
install_pkg() {
  if rpm -q "$1" &>/dev/null; then
    echo "    $1 already installed, skipping."
  else
    sudo dnf install -y "$1"
  fi
}

for pkg in gcc gcc-c++ cmake git make pkgconf-pkg-config \
  elfutils-devel fmt-devel catch2-devel \
  libtree-sitter-devel ncurses-devel; do
  install_pkg "$pkg"
done

# ---------------------------------------------------------------------------
# FTXUI: not packaged in Fedora's repos, build + install from source once.
# ---------------------------------------------------------------------------
FTXUI_VERSION="v5.0.0"
FTXUI_DIR="/tmp/ftxui-build"

if ! pkg-config --exists ftxui 2>/dev/null && [ ! -f /usr/local/lib64/libftxui-component.a ] && [ ! -f /usr/local/lib/libftxui-component.a ]; then
  echo "==> Building and installing FTXUI (${FTXUI_VERSION})..."
  rm -rf "${FTXUI_DIR}"
  git -c protocol.version=1 clone --depth 1 --branch "${FTXUI_VERSION}" \
    https://github.com/ArthurSonzogni/ftxui "${FTXUI_DIR}"
  cmake -S "${FTXUI_DIR}" -B "${FTXUI_DIR}/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFTXUI_BUILD_EXAMPLES=OFF \
    -DFTXUI_BUILD_DOCS=OFF \
    -DFTXUI_BUILD_TESTS=OFF
  cmake --build "${FTXUI_DIR}/build" -j"$(nproc)"
  sudo cmake --install "${FTXUI_DIR}/build"
  sudo ldconfig
else
  echo "==> FTXUI already installed, skipping."
fi

# ---------------------------------------------------------------------------
# popl: single-header library, not packaged. Just drop the header in
# /usr/local/include so CMake's find_path/target_include_directories
# picks it up like any other system header.
# ---------------------------------------------------------------------------
POPL_VERSION="v1.3.0"
POPL_HEADER="/usr/local/include/popl.hpp"

if [ ! -f "${POPL_HEADER}" ]; then
  echo "==> Installing popl.hpp (${POPL_VERSION})..."
  sudo curl -fsSL \
    "https://raw.githubusercontent.com/badaix/popl/${POPL_VERSION}/include/popl.hpp" \
    -o "${POPL_HEADER}"
else
  echo "==> popl.hpp already installed, skipping."
fi

# ---------------------------------------------------------------------------
# tree-sitter-c / tree-sitter-cpp grammars: not packaged individually;
# tree-sitter-devel gives you the core runtime (libtree-sitter), but the
# per-language grammars still need to be built from their generated
# parser.c sources. Small enough to build directly into /usr/local.
# ---------------------------------------------------------------------------
TS_GRAMMAR_DIR="/tmp/ts-grammars"

build_grammar() {
  local name="$1"
  local repo="$2"
  local tag="$3"
  local extra_src="${4:-}"

  if [ -f "/usr/local/lib/lib${name}.a" ] || [ -f "/usr/local/lib64/lib${name}.a" ]; then
    echo "==> ${name} already installed, skipping."
    return
  fi

  echo "==> Building ${name} (${tag})..."
  rm -rf "${TS_GRAMMAR_DIR}/${name}"
  git -c protocol.version=1 clone --depth 1 --branch "${tag}" \
    "${repo}" "${TS_GRAMMAR_DIR}/${name}"

  local objs=()
  gcc -c -O2 -fPIC "${TS_GRAMMAR_DIR}/${name}/src/parser.c" \
    -I "${TS_GRAMMAR_DIR}/${name}/src" \
    -o "${TS_GRAMMAR_DIR}/${name}-parser.o"
  objs+=("${TS_GRAMMAR_DIR}/${name}-parser.o")

  if [ -n "${extra_src}" ]; then
    gcc -c -O2 -fPIC "${TS_GRAMMAR_DIR}/${name}/src/${extra_src}" \
      -I "${TS_GRAMMAR_DIR}/${name}/src" \
      -o "${TS_GRAMMAR_DIR}/${name}-scanner.o"
    objs+=("${TS_GRAMMAR_DIR}/${name}-scanner.o")
  fi

  ar rcs "${TS_GRAMMAR_DIR}/lib${name}.a" "${objs[@]}"

  sudo install -Dm644 "${TS_GRAMMAR_DIR}/lib${name}.a" "/usr/local/lib/lib${name}.a"
}

build_grammar "tree-sitter-c" \
  "https://github.com/tree-sitter/tree-sitter-c" "v0.21.4"

build_grammar "tree-sitter-cpp" \
  "https://github.com/tree-sitter/tree-sitter-cpp" "v0.22.3" "scanner.c"

sudo ldconfig

echo ""
echo "==> All dependencies installed."
echo "    - elfutils, fmt, Catch2, tree-sitter (core): via dnf"
echo "    - FTXUI: built + installed to /usr/local"
echo "    - popl.hpp: installed to /usr/local/include"
echo "    - tree-sitter-c / tree-sitter-cpp: built + installed to /usr/local"
