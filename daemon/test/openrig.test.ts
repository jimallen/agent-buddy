import assert from 'node:assert/strict';
import test from 'node:test';
import {agentForRuntime, OpenRigClient} from '../src/openrig.js';

type JsonResponse = {ok: boolean; status: number; json(): Promise<unknown>};

function fakeFetch(routes: Record<string, unknown>) {
  const urls: string[] = [];
  const fetchImpl = async (url: string): Promise<JsonResponse> => {
    urls.push(url);
    const path = new URL(url).pathname;
    const key = Object.keys(routes).find(route => path.endsWith(route));
    if (key === undefined) return {ok: false, status: 404, json: async () => ({error: 'not found'})};
    return {ok: true, status: 200, json: async () => routes[key]};
  };
  return {fetchImpl, urls};
}

const summaries = [
  {id: 'rig-one', name: 'jims-team'},
  {id: 'rig-kernel', name: 'kernel'},
];

const jimsNodes = [
  {
    logicalId: 'cos',
    role: null,
    sessionStatus: 'running',
    startupStatus: 'ready',
    terminalActive: true,
    activityState: {activity: 'working', display: 'working', needsInput: {count: 0, reason: null}},
  },
  {logicalId: 'worker-1', role: null, sessionStatus: 'running', startupStatus: 'ready'},
];

test('snapshotRigs lists each non-kernel rig with its seats normalized', async () => {
  const {fetchImpl, urls} = fakeFetch({
    '/api/rigs/summary': summaries,
    '/api/rigs/rig-one/nodes': jimsNodes,
    '/api/rigs/rig-kernel/nodes': [],
  });
  const client = new OpenRigClient({baseUrl: 'http://127.0.0.1:7433/', fetchImpl});

  const rigs = await client.snapshotRigs();

  // The kernel rig is the operator rig, not a team: it never gets a character.
  assert.deepEqual(rigs.map(r => r.rigName), ['jims-team']);
  assert.ok(urls.some(u => u.endsWith('/api/rigs/summary')));
  assert.ok(urls.some(u => u.endsWith('/api/rigs/rig-one/nodes')));
  assert.ok(!urls.some(u => u.includes('rig-kernel/nodes')));

  const cos = rigs[0]!.seats.find(s => s.logicalId === 'cos')!;
  assert.equal(cos.activity, 'working');
  assert.equal(cos.needsInput, 0);
  assert.equal(cos.terminalActive, true);
  assert.equal(cos.sessionStatus, 'running');
});

test('a seat without an oracle answer reports honest absence, not guesses', async () => {
  const {fetchImpl} = fakeFetch({
    '/api/rigs/summary': [{id: 'rig-one', name: 'jims-team'}],
    '/api/rigs/rig-one/nodes': [
      // Pre-oracle-coverage runtimes get no activityState at all.
      {logicalId: 'cos', role: null, sessionStatus: 'running', startupStatus: 'ready'},
    ],
  });
  const client = new OpenRigClient({baseUrl: 'http://127.0.0.1:7433', fetchImpl});

  const [rigSnapshot] = await client.snapshotRigs();
  assert.equal(rigSnapshot!.seats[0]!.activity, null);
  assert.equal(rigSnapshot!.seats[0]!.needsInput, 0);
  assert.equal(rigSnapshot!.seats[0]!.terminalActive, null);
});

test('an unreachable daemon rejects so the coordinator can show no rigs', async () => {
  const fetchImpl = async () => {
    throw new Error('connect ECONNREFUSED 127.0.0.1:7433');
  };
  const client = new OpenRigClient({baseUrl: 'http://127.0.0.1:7433', fetchImpl});
  await assert.rejects(() => client.snapshotRigs(), /ECONNREFUSED/);
});

test('an HTTP error from the daemon rejects with the status', async () => {
  const fetchImpl = async (): Promise<JsonResponse> => ({ok: false, status: 500, json: async () => ({})});
  const client = new OpenRigClient({baseUrl: 'http://127.0.0.1:7433', fetchImpl});
  await assert.rejects(() => client.snapshotRigs(), /500/);
});

test('OpenRig runtime names map to the agent ids the device knows', () => {
  assert.equal(agentForRuntime('claude-code'), 'claude');
  assert.equal(agentForRuntime('codex'), 'codex');
  assert.equal(agentForRuntime('pi'), 'pi');
  assert.equal(agentForRuntime('terminal'), null);
  assert.equal(agentForRuntime(null), null);
});
