import assert from 'node:assert/strict';
import test from 'node:test';
import {rigFleetPacket} from '../src/rig-coordinator.js';
import type {RigCharacter} from '../src/openrig.js';

function rig(label: string, state: RigCharacter['state'], present = true, agent: string | null = null): RigCharacter {
  return {rigId: `id-${label}`, rigName: label, seatRef: 'exec.cos', state, present, agent};
}

test('the fleet packet is label:state pairs in roster order', () => {
  assert.equal(rigFleetPacket([rig('eztrack', 'working'), rig('jims-team', 'idle')]),
    'eztrack:w,jims-team:i');
});

test('attention, complete and away rigs map to their letters', () => {
  assert.equal(rigFleetPacket([rig('a', 'attention'), rig('b', 'complete'), rig('c', 'idle', false)]),
    'a:a,b:c,c:-');
});

test('labels are sanitized for the device: lowercase, dash-folded, twelve chars', () => {
  assert.equal(rigFleetPacket([rig('Jims Team!! 2024', 'working')]), 'jims-team-20:w');
});

test('the fleet is capped at four rigs', () => {
  assert.equal(rigFleetPacket([rig('a', 'idle'), rig('b', 'idle'), rig('c', 'idle'),
                               rig('d', 'idle'), rig('e', 'idle')]),
    'a:i,b:i,c:i,d:i');
});

test('the harness behind each rig travels as the optional third field', () => {
  assert.equal(rigFleetPacket([rig('eztrack', 'working', true, 'claude'), rig('jims-team', 'idle', true, 'pi')]),
    'eztrack:w:claude,jims-team:i:pi');
});

test('a rig without a known harness sends two fields, not an empty third', () => {
  assert.equal(rigFleetPacket([rig('eztrack', 'working')]), 'eztrack:w');
});

test('an unsafe harness name is dropped rather than corrupting the packet', () => {
  assert.equal(rigFleetPacket([rig('eztrack', 'working', true, 'Claude Code')]), 'eztrack:w');
});

test('no rigs means an empty packet, which clears the device', () => {
  assert.equal(rigFleetPacket([]), '');
});
