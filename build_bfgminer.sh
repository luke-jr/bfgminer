#!/usr/bin/env bash
# If the script was invoked with /bin/sh (which may not support
# `set -o pipefail`), re-exec under bash so the strict options work.
if [ -z "${BASH_VERSION-}" ]; then
  exec bash "$0" "$@"
fi
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  echo "Usage: $0 [--install-deps] [configure-args...]"
  echo "  --install-deps   Attempt to install common deps (apt/brew/pacman)"
  exit 1
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
fi

INSTALL_DEPS=0
if [[ "${1:-}" == "--install-deps" ]]; then
  INSTALL_DEPS=1
  shift
fi

if command -v apt-get >/dev/null 2>&1; then
  PKG_MANAGER=apt
elif command -v brew >/dev/null 2>&1; then
  PKG_MANAGER=brew
elif command -v pacman >/dev/null 2>&1; then
  PKG_MANAGER=pacman
else
  PKG_MANAGER=unknown
fi

if [[ $INSTALL_DEPS -eq 1 ]]; then
  case $PKG_MANAGER in
    apt)
      sudo apt update
      sudo apt install -y build-essential autoconf automake libtool pkg-config libcurl4-gnutls-dev libjansson-dev uthash-dev libncursesw5-dev libudev-dev libusb-1.0-0-dev libevent-dev libmicrohttpd-dev libhidapi-dev
      ;;
    brew)
      brew update
      brew install autoconf automake libtool pkg-config curl jansson uthash ncurses libusb libevent libmicrohttpd hidapi
      ;;
    pacman)
      sudo pacman -Syu --noconfirm
      sudo pacman -S --needed --noconfirm base-devel autoconf automake libtool pkgconf curl jansson uthash ncurses libusb libevent libmicrohttpd hidapi
      ;;
    *)
      echo "Unknown package manager. Please install dependencies manually as described in README." >&2
      ;;
  esac
fi

cd "$REPO_ROOT"

# Run autogen if present (useful when building from a git checkout)
if [[ -x ./autogen.sh ]]; then
  ./autogen.sh
fi

# Run configure with any user-supplied args
./configure "$@"

# Build
if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
else
  JOBS=1
fi
make -j"$JOBS"

echo "Build finished. You can run ./bfgminer from this directory or 'sudo make install' to install system-wide."
echo "For native Windows builds see windows-build.txt or use MSYS2/WSL as recommended in README."
