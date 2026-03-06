import * as fs   from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

function findBinary(context: vscode.ExtensionContext): string | undefined {
    // 1. User-configured path takes priority.
    const configured: string = vscode.workspace.getConfiguration('fusion').get('serverPath') ?? '';
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

export function activate(context: vscode.ExtensionContext): void {
    const serverBinary = findBinary(context);

    if (!serverBinary) {
        vscode.window.showErrorMessage(
            'Fusion: cannot find fusion_lsp. ' +
            'Run ./make.sh first, or set fusion.serverPath in VS Code settings.',
        );
        return;
    }

    const serverOptions: ServerOptions = {
        command:   serverBinary,
        args:      [],
        transport: TransportKind.stdio,
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'fusion' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.fusion'),
        },
    };

    client = new LanguageClient(
        'fusion',
        'Fusion Language Server',
        serverOptions,
        clientOptions,
    );

    client.start();
    context.subscriptions.push(client);
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
