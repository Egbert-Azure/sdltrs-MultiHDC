/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Egbert H. Schroeer
 *
 * VS Code extension entry point: registers the zbx debug adapter type
 * and launches adapter.py.
 *
 * Part of the zbx Debug Adapter Protocol bridge. Contains no zbx or
 * xtrs code. See LICENSE for the full BSD 2-Clause text.
 */

// /tools/zbx-dap/extension/extension.js
//
// The whole extension: register the "zbx" debug type and point it at
// adapter.py, which is the actual debug adapter. See ../adapter.py.
const vscode = require('vscode');
const path = require('path');

class ZbxDebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(_session) {
        const script = path.join(__dirname, 'adapter.py');
        return new vscode.DebugAdapterExecutable('python3', [script]);
    }
}

function activate(context) {
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory(
            'zbx', new ZbxDebugAdapterDescriptorFactory()
        )
    );
}

function deactivate() {}

module.exports = { activate, deactivate };
