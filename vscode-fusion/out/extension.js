"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const fs = __importStar(require("fs"));
const path = __importStar(require("path"));
const vscode = __importStar(require("vscode"));
const node_1 = require("vscode-languageclient/node");
let client;
function findBinary(context) {
    // 1. User-configured path takes priority.
    const configured = vscode.workspace.getConfiguration('fusion').get('serverPath') ?? '';
    if (configured && fs.existsSync(configured)) {
        return configured;
    }
    // 2. Default: build/fusion_lsp relative to the first workspace folder.
    //    Matches the RUNTIME_OUTPUT_DIRECTORY set in lsp/CMakeLists.txt.
    const folders = vscode.workspace.workspaceFolders;
    if (folders && folders.length > 0) {
        const candidate = path.join(folders[0].uri.fsPath, 'build', 'fusion_lsp');
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    // 3. Binary bundled inside the extension package (for packaged/installed releases).
    const bundled = path.join(context.extensionPath, 'bin', 'fusion_lsp');
    if (fs.existsSync(bundled)) {
        return bundled;
    }
    return undefined;
}
function activate(context) {
    const serverBinary = findBinary(context);
    if (!serverBinary) {
        vscode.window.showErrorMessage('Fusion: cannot find fusion_lsp. ' +
            'Run ./make.sh first, or set fusion.serverPath in VS Code settings.');
        return;
    }
    const serverOptions = {
        command: serverBinary,
        args: [],
        transport: node_1.TransportKind.stdio,
    };
    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'fusion' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.fusion'),
        },
    };
    client = new node_1.LanguageClient('fusion', 'Fusion Language Server', serverOptions, clientOptions);
    client.start();
    context.subscriptions.push(client);
}
function deactivate() {
    return client?.stop();
}
//# sourceMappingURL=extension.js.map