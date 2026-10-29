import { ReaClient } from './rea-client.mjs';
import { mkdir, readdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { setTimeout } from 'node:timers/promises';

const root = process.cwd();
const requests = path.join(root, '.analysis', 'requests');
await mkdir(requests, { recursive: true });
const client = new ReaClient(root);
const allowed = new Set([
  'open_binary', 'close_binary', 'export_evidence_bundle',
  'search_strings', 'search_procedures', 'xrefs', 'analyze_function',
  'procedure_pseudo_code', 'read_bytes', 'list_segments',
  'address_to_file_offset', 'read_function_instructions',
]);
await client.initialize();
const catalog = await client.request('tools/list');
await writeFile(path.join(root, '.analysis', 'tool-schemas.json'), JSON.stringify(
  catalog.tools.filter(t => allowed.has(t.name)), null, 2));
console.log('REA MCP runner ready');
const consumed = new Set();
let running = true;
while (running) {
  const names = (await readdir(requests)).filter(n => n.endsWith('.json')).sort();
  for (const name of names) {
    if (consumed.has(name)) continue;
    const commands = JSON.parse(await readFile(path.join(requests, name), 'utf8'));
    consumed.add(name);
    for (const command of commands) {
      if (command.shutdown) { running = false; break; }
      if (!allowed.has(command.tool)) throw new Error('Tool not allowed: ' + command.tool);
      if (!/^[a-zA-Z0-9_-]+$/.test(command.output)) throw new Error('Invalid output name');
      console.log('Starting', command.output, command.tool);
      const result = await client.call(command.tool, command.args, command.output);
      console.log('Finished', command.output, result.isError ? 'ERROR' : 'OK');
    }
  }
  if (running) await setTimeout(500);
}
await client.call('close_binary', {});
client.stop();
console.log('REA MCP runner closed');
