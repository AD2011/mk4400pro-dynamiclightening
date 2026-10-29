// Local stdio client for the installed REA MCP server, with a portable Ghidra.
// This analyzes the supplied files; it never launches the keyboard software.
import { spawn } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';

export class ReaClient {
  constructor(root) {
    this.root = root;
    this.nextId = 1;
    this.pending = new Map();
    this.stderr = '';
    this.buffer = '';
    const psQuote = value => "'" + value.replaceAll("'", "''") + "'";
    this.child = spawn('C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe', [
      '-NoProfile', '-NonInteractive', '-Command',
      '$env:GHIDRA_INSTALL_DIR=' + psQuote(path.join(root, '.analysis', 'tools', 'ghidra_12.1.4_PUBLIC')) +
      "; $env:REA_GHIDRA_STARTUP_TIMEOUT_MS='600000'; " +
      "; $env:JAVA_HOME='C:\\Program Files\\Eclipse Adoptium\\jdk-25.0.4.101-hotspot'; " +
      "& 'C:\\Program Files\\nodejs\\node.exe' 'C:\\Users\\abir\\AppData\\Roaming\\npm\\node_modules\\rea-agents\\scripts\\rea.mjs' --mcp",
    ], {
      cwd: root,
      windowsHide: true,
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.child.stderr.on('data', data => { this.stderr += data.toString(); });
    this.child.stdout.on('data', data => {
      this.buffer += data.toString();
      let newline;
      while ((newline = this.buffer.indexOf('\n')) >= 0) {
        const line = this.buffer.slice(0, newline);
        this.buffer = this.buffer.slice(newline + 1);
        if (!line.trim()) continue;
        let message;
        try { message = JSON.parse(line); }
        catch { this.stderr += `Non-JSON stdout: ${line}\n`; continue; }
        if (message.id !== undefined && this.pending.has(message.id)) {
          const waiter = this.pending.get(message.id);
          this.pending.delete(message.id);
          if (message.error) waiter.reject(new Error(JSON.stringify(message.error)));
          else waiter.resolve(message.result);
        }
      }
    });
    this.child.on('exit', code => {
      for (const waiter of this.pending.values())
        waiter.reject(new Error(`REA exited ${code}: ${this.stderr.slice(-4000)}`));
      this.pending.clear();
    });
    this.child.on('error', error => {
      for (const waiter of this.pending.values()) waiter.reject(error);
      this.pending.clear();
    });
  }
  request(method, params = {}) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
    });
  }
  async initialize() {
    const result = await this.request('initialize', {
      protocolVersion: '2024-11-05',
      capabilities: {},
      clientInfo: { name: 'mk4400pro-local-analysis', version: '1.0.0' },
    });
    this.child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
    return result;
  }
  async call(name, args, evidenceName) {
    const result = await this.request('tools/call', { name, arguments: args });
    if (evidenceName) {
      const directory = path.join(this.root, 'research', 'evidence');
      await mkdir(directory, { recursive: true });
      await writeFile(path.join(directory, evidenceName + '.json'), JSON.stringify(result, null, 2));
    }
    return result;
  }
  stop() { this.child.stdin.end(); }
}
