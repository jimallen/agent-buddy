import assert from 'node:assert/strict';
import {mkdir, readFile, rm, writeFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {dirname, join} from 'node:path';
import {randomUUID} from 'node:crypto';
import test from 'node:test';
import {piAdapter, piExtensionPath, piExtensionSource} from '../src/agents/pi.js';
import {defaultAgentContext} from '../src/agents/index.js';
import type {AgentContext} from '../src/agents/types.js';

async function touch(...paths: string[]): Promise<void> {
  for (const path of paths) {
    await mkdir(dirname(path), {recursive: true});
    await writeFile(path, '');
  }
}

async function fixture(): Promise<{root: string; home: string; ctx: AgentContext; cleanup: () => Promise<void>}> {
  const root = join(process.cwd(), '.test-output', `pi-${randomUUID()}`);
  const home = join(root, 'home');
  await mkdir(home, {recursive: true});
  const node = join(root, 'node bin', 'node');
  const cli = join(root, 'agent companion', 'daemon', 'dist', 'src', 'cli.js');
  await touch(node, cli);
  const ctx = defaultAgentContext({
    home,
    node,
    cli,
    platform: 'darwin',
    env: {PATH: ''},
    dataDir: join(root, 'data'),
    socketPath: join(root, 'daemon.sock'),
    runCommand: async () => ({stdout: '', status: 0}),
  });
  return {root, home, ctx, cleanup: async () => rm(root, {recursive: true, force: true})};
}

test('Pi install writes its extension and keeps other extensions', async () => {
  const {home, ctx, cleanup} = await fixture();
  try {
    const own = join(home, '.pi', 'agent', 'extensions', 'todo.ts');
    await touch(own);
    assert.equal(piAdapter.hookStatus(ctx), 'missing');

    await piAdapter.install(ctx);
    const path = piExtensionPath(ctx);
    assert.equal(existsSync(path), true);
    assert.equal(existsSync(own), true);

    const source = await readFile(path, 'utf8');
    // The embedded Node and CLI paths survive spaces in their folders.
    assert.ok(source.includes(JSON.stringify(ctx.node)), 'extension embeds this install\'s Node.js');
    assert.ok(source.includes(JSON.stringify(ctx.cli)), 'extension embeds this install\'s CLI');
    assert.equal(piAdapter.hookStatus(ctx), 'installed');

    // Installing again is a rewrite, not a second copy.
    await piAdapter.install(ctx);
    assert.equal(piAdapter.hookStatus(ctx), 'installed');
  } finally {
    await cleanup();
  }
});

test('an extension from another install or an older version shows as outdated', async () => {
  const {home, ctx, cleanup} = await fixture();
  try {
    await piAdapter.install(ctx);
    const path = piExtensionPath(ctx);
    const older = piExtensionSource(join(home, 'old node'), ctx.cli, 0);
    await writeFile(path, older);
    assert.equal(piAdapter.hookStatus(ctx), 'outdated');
    const moved = piExtensionSource(ctx.node, join(home, 'elsewhere', 'cli.js'));
    await writeFile(path, moved);
    assert.equal(piAdapter.hookStatus(ctx), 'outdated');
  } finally {
    await cleanup();
  }
});

test('Pi uninstall removes only our extension file', async () => {
  const {ctx, home, cleanup} = await fixture();
  try {
    const own = join(home, '.pi', 'agent', 'extensions', 'todo.ts');
    await touch(own);
    await piAdapter.install(ctx);
    await piAdapter.uninstall(ctx);
    assert.equal(existsSync(piExtensionPath(ctx)), false);
    assert.equal(existsSync(own), true);
    assert.equal(piAdapter.hookStatus(ctx), 'missing');
    // Uninstalling with nothing installed is not an error.
    await piAdapter.uninstall(ctx);
  } finally {
    await cleanup();
  }
});

test('the extension source does not start sessions: handlers register only', () => {
  const source = piExtensionSource('/opt/node/bin/node', '/opt/agent-companion/cli.js');
  // Every daemon touch happens inside an event handler; the factory itself spawns nothing.
  assert.equal(source.includes('pi.on('), true);
  assert.ok(!/export default function[\s\S]*?daemon\(/.test(source.split('pi.on(')[0] ?? ''),
    'no event is sent during extension load');
});

test('Pi events normalize to the coordinator vocabulary', () => {
  const received = 1234;
  assert.deepEqual(piAdapter.normalize('sessionStart', {sessionId: '/tmp/pi/a.jsonl'}, received),
    [{event: 'sessionStart', payload: {sessionId: 'pi:/tmp/pi/a.jsonl', timestamp: received}}]);
  assert.deepEqual(piAdapter.normalize('userPromptSubmitted', {sessionId: '/tmp/pi/a.jsonl'}, received),
    [{event: 'userPromptSubmitted', payload: {sessionId: 'pi:/tmp/pi/a.jsonl', timestamp: received}}]);
  assert.deepEqual(piAdapter.normalize('preToolUse', {sessionId: '/tmp/pi/a.jsonl', toolName: 'bash', toolCallId: 't1'}, received),
    [{event: 'preToolUse', payload: {sessionId: 'pi:/tmp/pi/a.jsonl', toolName: 'bash', toolCallId: 't1', timestamp: received}}]);
  assert.equal(piAdapter.normalize('postToolUse', {sessionId: '/tmp/pi/a.jsonl'}, received)[0]?.event, 'postToolUse');
  assert.equal(piAdapter.normalize('postToolUseFailure', {sessionId: '/tmp/pi/a.jsonl'}, received)[0]?.event, 'postToolUseFailure');
  assert.equal(piAdapter.normalize('agentStop', {sessionId: '/tmp/pi/a.jsonl'}, received)[0]?.event, 'agentStop');
  assert.equal(piAdapter.normalize('sessionEnd', {sessionId: '/tmp/pi/a.jsonl'}, received)[0]?.event, 'sessionEnd');
  assert.deepEqual(piAdapter.normalize('idle_tick', {}, received), []);
});

test('Pi is detected from its command or its agent directory', async () => {
  const {home, ctx, cleanup} = await fixture();
  try {
    assert.equal(piAdapter.detect(ctx).installed, false);
    await touch(join(home, '.pi', 'agent', 'settings.json'));
    const detected = piAdapter.detect(ctx);
    assert.equal(detected.installed, true);
    assert.equal(detected.configPath, join(home, '.pi', 'agent'));
  } finally {
    await cleanup();
  }
});
