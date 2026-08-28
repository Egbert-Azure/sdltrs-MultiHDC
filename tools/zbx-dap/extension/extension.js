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
