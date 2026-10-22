import assert from 'node:assert/strict';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import test from 'node:test';
import {CompanionService} from '../src/companion-service.js';
import {DeviceTransport} from '../src/device-transport.js';
import {defaultOpenrigUrl} from '../src/display-settings.js';
import {StateCoordinator} from '../src/state-coordinator.js';
import {defaultAgentContext} from '../src/agents/index.js';
import {OnboardingStore} from '../src/onboarding.js';
import type {RigView} from '../src/rig-coordinator.js';

async function fixture() {
  const root = await mkdtemp(join(tmpdir(), 'openrig-service-'));
  const environment = {
    AGENT_COMPANION_AGENTS_CONFIG: join(root, 'agents.json'),
    AGENT_COMPANION_DISPLAY_CONFIG: join(root, 'display.json'),
    AGENT_COMPANION_CHARACTER_CONFIG: join(root, 'character.json'),
    AGENT_COMPANION_CONNECTION_CONFIG: join(root, 'connection.json'),
    AGENT_COMPANION_ONBOARDING: join(root, 'onboarding.json'),
  };
  const previousEnvironment = Object.fromEntries(Object.keys(environment).map(key => [key, process.env[key]]));
  Object.assign(process.env, environment);
  const transports: string[] = [];
  const transport = new DeviceTransport(() => undefined, () => undefined);
  const coordinator = new StateCoordinator(() => undefined);
  const context = defaultAgentContext({home: root, dataDir: root, env: {PATH: '', ...environment}});
  const preferences: {enabled: boolean; url: string}[] = [];
  const service = new CompanionService(transport, coordinator, context, [], {refresh: async () => undefined},
    undefined, {refresh: async () => false, totals: () => ({aic: null, tokens: null})},
    () => null, undefined, undefined, pref => preferences.push(pref));
  return {
    service, root, transports, preferences,
    async cleanup() {
      coordinator.close();
      for (const [key, value] of Object.entries(previousEnvironment)) {
        if (value === undefined) delete process.env[key];
        else process.env[key] = value;
      }
      await rm(root, {recursive: true, force: true});
    },
  };
}

const jimsTeam: RigView = {
  reachable: true,
  rigs: [{rigId: 'rig-1', rigName: 'jims-team', seatRef: 'cos', state: 'working', present: true, agent: 'claude'}],
};

test('OpenRig is off by default, with the local daemon address', async () => {
  const {service, cleanup} = await fixture();
  try {
    assert.deepEqual(service.status().openrig, {enabled: false, url: defaultOpenrigUrl, reachable: false, rigs: []});
  } finally {
    await cleanup();
  }
});

test('turning OpenRig on persists the preference and tells the daemon', async () => {
  const {service, preferences, root, cleanup} = await fixture();
  try {
    await service.setOpenrig({enabled: true, url: 'http://127.0.0.1:9999'});
    assert.equal(service.status().openrig.enabled, true);
    assert.equal(service.status().openrig.url, 'http://127.0.0.1:9999');
    assert.deepEqual(preferences, [{enabled: true, url: 'http://127.0.0.1:9999'}]);
    const saved = JSON.parse(await readFile(join(root, 'display.json'), 'utf8'));
    assert.equal(saved.openrigEnabled, true);
    assert.equal(saved.openrigUrl, 'http://127.0.0.1:9999');
  } finally {
    await cleanup();
  }
});

test('rig views from the coordinator show in status', async () => {
  const {service, cleanup} = await fixture();
  try {
    await service.setOpenrig({enabled: true});
    service.setRigView(jimsTeam);
    assert.deepEqual(service.status().openrig.rigs, jimsTeam.rigs);
    assert.equal(service.status().openrig.reachable, true);
  } finally {
    await cleanup();
  }
});

test('turning OpenRig off clears the rigs from status', async () => {
  const {service, preferences, cleanup} = await fixture();
  try {
    await service.setOpenrig({enabled: true});
    service.setRigView(jimsTeam);
    await service.setOpenrig({enabled: false});
    assert.deepEqual(service.status().openrig, {enabled: false, url: defaultOpenrigUrl, reachable: false, rigs: []});
    assert.deepEqual(preferences[1], {enabled: false, url: defaultOpenrigUrl});
  } finally {
    await cleanup();
  }
});

test('a bad daemon address is rejected', async () => {
  const {service, cleanup} = await fixture();
  try {
    await assert.rejects(service.setOpenrig({url: 'not a url'}), /http/);
    await assert.rejects(service.setOpenrig({url: 'ftp://127.0.0.1:7433'}), /http/);
    assert.equal(service.status().openrig.url, defaultOpenrigUrl);
  } finally {
    await cleanup();
  }
});
