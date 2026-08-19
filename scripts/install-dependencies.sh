#!/usr/bin/env bash

set -euo pipefail

dependency_group="${1:-all}"
if [[ "${dependency_group}" != "all" && "${dependency_group}" != "gl" ]]; then
  echo "Usage: $0 [all|gl]" >&2
  exit 2
fi

platform="$(uname -s)"
case "${platform}" in
  Linux)
    if ! command -v apt-get >/dev/null 2>&1; then
      echo "Unsupported Linux package manager; install GLFW 3.3 or newer manually." >&2
      exit 1
    fi
    apt_prefix=()
    if [[ "$(id -u)" -ne 0 ]]; then
      apt_prefix=(sudo)
    fi
    "${apt_prefix[@]}" apt-get update
    "${apt_prefix[@]}" apt-get install -y libglfw3-dev
    ;;
  Darwin)
    if ! command -v brew >/dev/null 2>&1; then
      echo "Homebrew is required; install it first or install GLFW 3.3 or newer manually." >&2
      exit 1
    fi
    brew install glfw
    ;;
  MINGW*|MSYS*|CYGWIN*)
    vcpkg_root="${VCPKG_ROOT:-${VCPKG_INSTALLATION_ROOT:-}}"
    if [[ -z "${vcpkg_root}" ]]; then
      echo "Set VCPKG_ROOT or VCPKG_INSTALLATION_ROOT to an existing vcpkg installation." >&2
      exit 1
    fi
    if command -v cygpath >/dev/null 2>&1; then
      vcpkg_root="$(cygpath -u "${vcpkg_root}")"
    fi
    vcpkg_executable="${vcpkg_root}/vcpkg"
    if [[ -x "${vcpkg_root}/vcpkg.exe" ]]; then
      vcpkg_executable="${vcpkg_root}/vcpkg.exe"
    fi
    if [[ ! -x "${vcpkg_executable}" ]]; then
      echo "vcpkg executable not found under ${vcpkg_root}." >&2
      exit 1
    fi
    "${vcpkg_executable}" install "glfw3:${VCPKG_DEFAULT_TRIPLET:-x64-windows}"
    ;;
  *)
    echo "Unsupported platform ${platform}; install GLFW 3.3 or newer manually." >&2
    exit 1
    ;;
esac
