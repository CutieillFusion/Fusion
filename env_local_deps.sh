#!/usr/bin/env bash
# Source this before configuring/building to use libffi and zlib from ~/.local.
# Example: source ./env_local_deps.sh && ./test.sh

export PKG_CONFIG_PATH="${HOME}/.local/lib/pkgconfig:${HOME}/.local/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export CMAKE_PREFIX_PATH="${HOME}/.local${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export CPATH="${HOME}/.local/include${CPATH:+:${CPATH}}"
export LIBRARY_PATH="${HOME}/.local/lib:${HOME}/.local/lib64${LIBRARY_PATH:+:${LIBRARY_PATH}}"
export LD_LIBRARY_PATH="${HOME}/.local/lib:${HOME}/.local/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
