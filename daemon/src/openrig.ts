// OpenRig (github.com/mvschwarz/openrig) integration: reads each rig's chief-of-staff
// seat from the OpenRig daemon's HTTP API and folds it into one character per rig.
//
// The OpenRig daemon (default http://127.0.0.1:7433) exposes:
//   GET /api/rigs/summary   — one summary per active rig (id, name)
//   GET /api/rigs/:id/graph — per-seat inventory, including the arbitrated activity
//                             oracle (activityState) and terminal activity
//   GET /api/activity/events — SSE seat.activity_changed notifications
// We consume the smallest honest projection of each surface; fields absent from a
// response stay absent here rather than being guessed.
import type {CharacterState} from './protocol.js';

// One seat's signals, as the rig graph reports them. All of this is observed OpenRig
// state; a null means the daemon did not answer that signal for this seat.
export interface CosCandidate {
  // Seat addressing: logicalId is the human address ("cos", "exec-cos"); role is the
  // spec's declared role such as "chief-of-staff".
  logicalId: string;
  role: string | null;
  // Lifecycle: sessionStatus is the current session's status ("running", "stopped", …),
  // startupStatus is the last startup outcome.
  sessionStatus: string | null;
  startupStatus: string | null;
  // The arbitrated activity oracle: "working", "idle-at-prompt", "idle", or null when
  // no rung has answered for this seat yet.
  activity: string | null;
  needsInput: number;
  // The tmux byte-stream primitive, when observed; distinct from the oracle.
  terminalActive: boolean | null;
  // The agent runtime the seat runs ("claude-code", "codex", "pi", …), as OpenRig names it.
  runtime: string | null;
}

// One rig as the OpenRig daemon reports it.
export interface RigSnapshot {
  rigId: string;
  rigName: string;
  seats: CosCandidate[];
}

// One rig for the display: its chief of staff, folded to a character state.
// present is false when no chief-of-staff seat exists or its session is not running;
// the display should show the rig dimmed (the seat is part of the rig but away).
export interface RigCharacter {
  rigId: string;
  rigName: string;
  // The addressing name of the seat that stands for this rig, or null when there is none.
  seatRef: string | null;
  state: CharacterState;
  present: boolean;
  // The agent runtime of that seat, mapped to our agent id ("claude", "codex", "pi"), or null
  // when the daemon did not report one. The device shows this agent's icon per rig, because
  // one flashed character pack serves every column.
  agent: string | null;
}

// OpenRig's runtime names mapped to the agent ids the device knows, so a rig column can show
// the badge of the agent behind that rig's chief of staff.
export function agentForRuntime(runtime: string | null): string | null {
  switch (runtime) {
    case 'claude-code': return 'claude';
    case 'codex': return 'codex';
    case 'pi': return 'pi';
    case 'omp': return 'omp';
    case 'terminal': return null;
    default: return runtime && /^[a-z0-9-]+$/.test(runtime) ? runtime : null;
  }
}

export type CosMatcher = (seat: CosCandidate) => boolean;

export interface FetchResponse {
  ok: boolean;
  status: number;
  json(): Promise<unknown>;
}

export type FetchImpl = (url: string) => Promise<FetchResponse>;

export interface OpenRigClientOptions {
  baseUrl: string;
  fetchImpl?: FetchImpl;
}

function text(value: unknown): string | null {
  return typeof value === 'string' && value ? value : null;
}

function number(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function bool(value: unknown): boolean | null {
  return typeof value === 'boolean' ? value : null;
}

function object(value: unknown): Record<string, unknown> | null {
  return value != null && typeof value === 'object' && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : null;
}

// One seat row of GET /api/rigs/:rigId/nodes. Everything unknown stays null: the rig
// graph's "honest absence" rule applies here too.
function toCosCandidate(node: Record<string, unknown>): CosCandidate | null {
  const logicalId = text(node['logicalId']);
  if (!logicalId) return null;
  const activityState = object(node['activityState']);
  const needsInput = object(activityState?.['needsInput']);
  return {
    logicalId,
    role: text(node['role']),
    sessionStatus: text(node['sessionStatus']),
    startupStatus: text(node['startupStatus']),
    activity: text(activityState?.['activity']),
    needsInput: number(needsInput?.['count']),
    terminalActive: bool(node['terminalActive']),
    runtime: text(node['runtime']),
  };
}

// The OpenRig daemon's HTTP client. Default daemon address; the rig list comes from
// /api/rigs/summary and each rig's seats from /api/rigs/:id/nodes. The kernel rig is
// the operator rig, not a team, so it is excluded by name.
export class OpenRigClient {
  readonly #baseUrl: string;
  readonly #fetch: FetchImpl;

  constructor(options: OpenRigClientOptions) {
    this.#baseUrl = options.baseUrl.replace(/\/+$/, '');
    this.#fetch = options.fetchImpl ?? (async url => (await fetch(url)) as FetchResponse);
  }

  async snapshotRigs(): Promise<RigSnapshot[]> {
    const summaries = await this.#getJson('/api/rigs/summary');
    if (!Array.isArray(summaries)) throw new Error('OpenRig /api/rigs/summary did not return an array.');
    const rigs = summaries
      .map(object)
      .filter((s): s is Record<string, unknown> => s != null)
      .map(s => ({rigId: text(s['id']), rigName: text(s['name'])}))
      .filter((r): r is {rigId: string; rigName: string} => r.rigId != null && r.rigName != null)
      .filter(r => r.rigName !== 'kernel');
    return Promise.all(rigs.map(async r => ({...r, seats: await this.#rigSeats(r.rigId)})));
  }

  async #rigSeats(rigId: string): Promise<CosCandidate[]> {
    const nodes = await this.#getJson(`/api/rigs/${encodeURIComponent(rigId)}/nodes`);
    if (!Array.isArray(nodes)) throw new Error(`OpenRig rig ${rigId} nodes did not return an array.`);
    return nodes.map(object).filter((n): n is Record<string, unknown> => n != null).map(toCosCandidate).filter(
      (c): c is CosCandidate => c != null,
    );
  }

  async #getJson(path: string): Promise<unknown> {
    const response = await this.#fetch(`${this.#baseUrl}${path}`);
    if (!response.ok) throw new Error(`OpenRig ${path} answered HTTP ${response.status}.`);
    return response.json();
  }
}

// The default chief-of-staff convention: a "cos" seat, in a pod ("exec.cos") or named
// "exec-cos" style, or a seat whose declared role is chief-of-staff. Case-insensitive
// so "CoS" specs match.
export const defaultCosMatcher: CosMatcher = seat =>
  seat.role?.toLowerCase() === 'chief-of-staff' ||
  seat.logicalId.toLowerCase() === 'cos' ||
  seat.logicalId.toLowerCase().endsWith('-cos') ||
  seat.logicalId.toLowerCase().endsWith('.cos');

export function selectCosSeat(seats: CosCandidate[], matcher: CosMatcher = defaultCosMatcher): CosCandidate | null {
  return seats.find(matcher) ?? null;
}

// One mapping, one place: the CoS seat's own signals decide the rig's character. A
// worker needing attention does not move the character; the chief of staff is the
// interface to the rig. 'complete' and 'surprise' stay with the hook-driven single
// character — a persistent seat has no completion moment.
export function foldRigCharacter(
  snapshot: RigSnapshot,
  matcher: CosMatcher = defaultCosMatcher,
): RigCharacter {
  const seat = selectCosSeat(snapshot.seats, matcher);
  const base = {rigId: snapshot.rigId, rigName: snapshot.rigName, agent: agentForRuntime(seat?.runtime ?? null)};
  if (!seat) return {...base, seatRef: null, state: 'idle', present: false};

  const seatRef = seat.logicalId;
  if (seat.sessionStatus !== 'running') return {...base, seatRef, state: 'idle', present: false};
  if (seat.startupStatus === 'attention_required' || seat.startupStatus === 'failed') {
    return {...base, seatRef, state: 'attention', present: true};
  }
  if (seat.needsInput > 0) return {...base, seatRef, state: 'attention', present: true};
  if (seat.activity === 'working') return {...base, seatRef, state: 'working', present: true};
  // The oracle has not answered yet: fall back to the terminal byte-stream, honestly.
  if (seat.activity == null && seat.terminalActive === true) {
    return {...base, seatRef, state: 'working', present: true};
  }
  return {...base, seatRef, state: 'idle', present: true};
}
