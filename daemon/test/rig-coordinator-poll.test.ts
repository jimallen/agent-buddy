import assert from 'node:assert/strict';
import test from 'node:test';
import {RigCoordinator} from '../src/rig-coordinator.js';
import type {RigSnapshot} from '../src/openrig.js';

function fixture(snapshots: () => Promise<RigSnapshot[]>) {
  const emissions: {reachable: boolean; rigs: unknown[]}[] = [];
  const coordinator = new RigCoordinator({
    poll: snapshots,
    sweepMs: 0,
    onChange: view => emissions.push(view),
  });
  return {coordinator, emissions};
}

const cosRunning = (activity = 'working'): RigSnapshot[] => [{
  rigId: 'rig-1',
  rigName: 'jims-team',
  seats: [{
    logicalId: 'cos', role: null, sessionStatus: 'running', startupStatus: 'ready',
    activity, needsInput: 0, terminalActive: null, runtime: 'claude-code',
  }],
}];

test('pollOnce folds each rig and reports what changed', async () => {
  const {coordinator, emissions} = fixture(async () => cosRunning());
  await coordinator.pollOnce();
  assert.equal(emissions.length, 1);
  assert.equal(emissions[0]!.reachable, true);
  assert.deepEqual(emissions[0]!.rigs, [{
    rigId: 'rig-1',
    rigName: 'jims-team',
    seatRef: 'cos',
    state: 'working',
    present: true,
    agent: 'claude',
  }]);
});

test('identical snapshots do not re-emit', async () => {
  const {coordinator, emissions} = fixture(async () => cosRunning());
  await coordinator.pollOnce();
  await coordinator.pollOnce();
  assert.equal(emissions.length, 1);
});

test('a state change emits again', async () => {
  let working = true;
  const {coordinator, emissions} = fixture(async () => cosRunning(working ? 'working' : 'idle-at-prompt'));
  await coordinator.pollOnce();
  working = false;
  await coordinator.pollOnce();
  assert.equal(emissions.length, 2);
  assert.equal(emissions[1]!.rigs[0] && (emissions[1]!.rigs[0] as {state: string}).state, 'idle');
});

test('an unreachable OpenRig daemon shows no rigs and is not an error', async () => {
  const {coordinator, emissions} = fixture(async () => {
    throw new Error('connect ECONNREFUSED');
  });
  await coordinator.pollOnce();
  assert.deepEqual(emissions, [{reachable: false, rigs: []}]);
});

test('the daemon coming back recovers without a restart', async () => {
  let down = true;
  const {coordinator, emissions} = fixture(async () => {
    if (down) throw new Error('connect ECONNREFUSED');
    return cosRunning();
  });
  await coordinator.pollOnce();
  down = false;
  await coordinator.pollOnce();
  assert.equal(emissions.length, 2);
  assert.equal(emissions[1]!.reachable, true);
  assert.equal(emissions[1]!.rigs.length, 1);
});
