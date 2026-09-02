#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Egbert H. Schroeer
#
# Installs the local zbx debug-adapter VS Code extension into the
# user's VS Code extensions directory.
#
# Part of the zbx Debug Adapter Protocol bridge. Contains no zbx or
# xtrs code. See LICENSE for the full BSD 2-Clause text.

# /tools/zbx-dap/install-extension.sh
#
# Installs the local "zbx" debug type extension into VS Code's user
# extensions directory. Unpublished, local-only -- see ../adapter.py and
# extension/. Re-run after editing extension.js or package.json; restart
# VS Code (or "Developer: Reload Window") afterwards to pick up changes.
set -e
cd "$(dirname "$0")"

# Install into every VS Code variant found (stable, Insiders, ...) since
# each reads extensions from its own directory.
installed_any=false
for extdir in "$HOME/.vscode/extensions" "$HOME/.vscode-insiders/extensions"; do
    [ -d "$(dirname "$extdir")" ] || continue
    DEST="$extdir/local.zbx-debug-0.0.1"
    rm -rf "$DEST"
    mkdir -p "$DEST"
    cp extension/package.json extension/extension.js adapter.py "$DEST/"
    echo "Installed to $DEST"
    installed_any=true
done

if ! $installed_any; then
    echo "No VS Code user directory found (~/.vscode or ~/.vscode-insiders)." >&2
    exit 1
fi

echo "Restart VS Code (or run \"Developer: Reload Window\") to pick it up."
