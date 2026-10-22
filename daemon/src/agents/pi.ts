// Pi (the coding agent from earendil-works) has no hook config; it runs TypeScript
// extensions. Our hook is one personal extension, ~/.pi/agent/extensions/agent-buddy.ts,
// whose handlers report the daemon's canonical events through the companion CLI, the same
// way the other agents' native hooks do.
import {existsSync, readFileSync} from 'node:fs';
import {mkdir, rm, writeFile} from 'node:fs/promises';
import {dirname, join} from 'node:path';
import {canonicalEvent, normalized} from './normalize.js';
import {hasCommand, versionOf} from './commands.js';
import {piHome} from './homes.js';
import type {AgentAdapter, AgentContext, HookStatus} from './types.js';

// Bump when the extension's content changes; older installs then read as outdated.
export const piExtensionVersion = 1;

const marker = '// Agent Buddy watches Pi sessions for the companion device';

export const piAdapter: AgentAdapter = {
  id: 'pi',
  name: 'Pi',
  hint: status => status === 'missing'
    ? 'Install the Agent Buddy extension, then restart Pi.'
    : undefined,
  detect(ctx) {
    const configPath = piHome(ctx.home, ctx.env);
    return {installed: hasCommand('pi', ctx) || existsSync(configPath), version: versionOf('pi', ctx), configPath};
  },
  hookStatus(ctx) {
    return piHookStatus(ctx);
  },
  async install(ctx) {
    const path = piExtensionPath(ctx);
    await mkdir(dirname(path), {recursive: true});
    await writeFile(path, piExtensionSource(ctx.node, ctx.cli), {mode: 0o644});
  },
  async uninstall(ctx) {
    const path = piExtensionPath(ctx);
    if (isOurExtension(path)) await rm(path, {force: true});
  },
  normalize(nativeEvent, payload, receiptTime) {
    // The extension already reports the daemon's canonical event names.
    return normalized('pi', canonicalEvent(nativeEvent, payload), payload, receiptTime);
  },
};

export function piExtensionPath(ctx: AgentContext): string {
  return join(piHome(ctx.home, ctx.env), 'extensions', 'agent-buddy.ts');
}

// The generated Pi extension. Every daemon touch runs inside an event handler — the
// factory itself spawns nothing, because Pi also loads extensions without a session.
export function piExtensionSource(node: string, cli: string, version = piExtensionVersion): string {
  return `${marker}. Nothing else in this file is hand-edited:
// the companion service's Install action rewrites it.
// agent-buddy-version: ${version}
// node: ${JSON.stringify(node)}
// cli: ${JSON.stringify(cli)}
const {spawn} = require('node:child_process');

const NODE = ${JSON.stringify(node)};
const CLI = ${JSON.stringify(cli)};

function report(event, payload) {
  try {
    const child = spawn(NODE, [CLI, 'hook', 'pi', event], {stdio: ['pipe', 'ignore', 'ignore']});
    child.on('error', () => {});
    child.stdin.end(JSON.stringify(payload ?? {}));
    child.unref();
  } catch {
    // The device is decoration; it must never block or fail a Pi session.
  }
}

function sessionPayload(ctx, extra) {
  let sessionId;
  try {
    sessionId = ctx.sessionManager?.getSessionFile?.() ?? undefined;
  } catch {
    sessionId = undefined;
  }
  return sessionId === undefined ? {...extra} : {sessionId, ...extra};
}

export default function (pi) {
  pi.on('session_start', (_event, ctx) => report('sessionStart', sessionPayload(ctx)));
  pi.on('before_agent_start', (event, ctx) =>
    report('userPromptSubmitted', sessionPayload(ctx, {pi_message_type: event.messageType})));
  pi.on('tool_execution_start', (event, ctx) =>
    report('preToolUse', sessionPayload(ctx, {toolName: event.toolName, toolCallId: event.toolCallId})));
  pi.on('tool_execution_end', (event, ctx) =>
    report(event.isError ? 'postToolUseFailure' : 'postToolUse',
      sessionPayload(ctx, {toolName: event.toolName, toolCallId: event.toolCallId})));
  pi.on('agent_end', (_event, ctx) => report('agentStop', sessionPayload(ctx)));
  pi.on('session_shutdown', (_event, ctx) => report('sessionEnd', sessionPayload(ctx)));
}
`;
}

function isOurExtension(path: string): boolean {
  try {
    return readFileSync(path, 'utf8').startsWith(marker);
  } catch {
    return false;
  }
}

function piHookStatus(ctx: AgentContext): HookStatus {
  if (!isOurExtension(piExtensionPath(ctx))) return 'missing';
  const source = readFileSync(piExtensionPath(ctx), 'utf8');
  const version = /^\/\/ agent-buddy-version: (\d+)$/m.exec(source);
  const node = /^\/\/ node: (".*")$/m.exec(source);
  const cli = /^\/\/ cli: (".*")$/m.exec(source);
  if (Number(version?.[1]) !== piExtensionVersion) return 'outdated';
  if (node?.[1] !== JSON.stringify(ctx.node) || cli?.[1] !== JSON.stringify(ctx.cli)) return 'outdated';
  // A moved or deleted Node.js or CLI leaves the extension reporting to nowhere.
  if (!existsSync(ctx.hostPath?.(ctx.node) ?? ctx.node) || !existsSync(ctx.hostPath?.(ctx.cli) ?? ctx.cli)) return 'outdated';
  return 'installed';
}
