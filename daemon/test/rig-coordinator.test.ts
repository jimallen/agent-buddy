import assert from 'node:assert/strict';
import test from 'node:test';
import {foldRigCharacter, selectCosSeat, type CosCandidate, type RigSnapshot} from '../src/openrig.js';

function seat(overrides: Partial<CosCandidate> = {}): CosCandidate {
  return {
    logicalId: 'worker',
    role: null,
    sessionStatus: 'running',
    startupStatus: 'ready',
    activity: 'idle-at-prompt',
    needsInput: 0,
    terminalActive: null,
    runtime: null,
    ...overrides,
  };
}

function rig(overrides: Partial<RigSnapshot> = {}): RigSnapshot {
  return {
    rigId: 'rig-1',
    rigName: 'jims-team',
    seats: [seat({logicalId: 'cos'}), seat({logicalId: 'worker-1'})],
    ...overrides,
  };
}

test('selects the seat whose logicalId is cos', () => {
  const selected = selectCosSeat(rig().seats);
  assert.equal(selected?.logicalId, 'cos');
});

test('selects an exec-cos style seat name', () => {
  const seats = [seat({logicalId: 'worker-1'}), seat({logicalId: 'exec-cos'})];
  assert.equal(selectCosSeat(seats)?.logicalId, 'exec-cos');
});

test('selects the cos seat inside a pod (exec.cos)', () => {
  const seats = [seat({logicalId: 'build.impl'}), seat({logicalId: 'exec.cos'})];
  assert.equal(selectCosSeat(seats)?.logicalId, 'exec.cos');
});

test('selects a seat with the chief-of-staff role', () => {
  const seats = [seat({logicalId: 'lead', role: 'chief-of-staff'})];
  assert.equal(selectCosSeat(seats)?.logicalId, 'lead');
});

test('a rig with no chief of staff selects nothing', () => {
  assert.equal(selectCosSeat([seat({logicalId: 'worker-1'})]), null);
});

test('cos needs-input shows the character asking for attention', () => {
  const character = foldRigCharacter(rig({seats: [seat({logicalId: 'cos', needsInput: 1})]}));
  assert.equal(character.state, 'attention');
  assert.equal(character.present, true);
});

test('cos working shows the character working', () => {
  const character = foldRigCharacter(rig({seats: [seat({logicalId: 'cos', activity: 'working'})]}));
  assert.equal(character.state, 'working');
});

test('cos at an idle prompt leaves the character idle', () => {
  const character = foldRigCharacter(rig());
  assert.equal(character.state, 'idle');
  assert.equal(character.present, true);
});

test('cos startup failure asks for attention even when the oracle is silent', () => {
  const character = foldRigCharacter(rig({seats: [seat({logicalId: 'cos', startupStatus: 'attention_required'})]}));
  assert.equal(character.state, 'attention');
});

test('a stopped cos shows as idle and away', () => {
  const character = foldRigCharacter(rig({seats: [seat({logicalId: 'cos', sessionStatus: 'stopped'})]}));
  assert.equal(character.state, 'idle');
  assert.equal(character.present, false);
});

test('a rig with no cos is absent, not idle', () => {
  const character = foldRigCharacter(rig({seats: [seat({logicalId: 'worker-1'})]}));
  assert.equal(character.present, false);
  assert.equal(character.seatRef, null);
});
