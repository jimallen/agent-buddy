// Watches the OpenRig daemon and keeps one character per rig, driven by that rig's
// chief-of-staff seat only. Polls on a sweep; emits one view when anything — rig set,
// reachability, or a character state — actually changes.
import {foldRigCharacter, type CosMatcher, type RigCharacter, type RigSnapshot} from './openrig.js';

// The fleet view needs protocol 13 firmware; older devices keep the single-character view.
export function shouldSendRigs(protocol: number): boolean {
  return Number.isInteger(protocol) && protocol >= 13;
}

// The device-side label alphabet is lowercase letters, digits and dashes; a label is at
// most twelve characters. The fleet view shows at most the first four rigs.
export function rigFleetPacket(rigs: readonly RigCharacter[]): string {
  const letters = {idle: 'i', working: 'w', complete: 'c', attention: 'a', surprise: 's'} as const;
  return rigs.slice(0, 4).map(rig => {
    const label = rig.rigName.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/-+/g, '-')
      .replace(/(^-|-$)/g, '').slice(0, 12) || 'rig';
    const state = rig.present ? (letters[rig.state] ?? 'i') : '-';
    // The harness behind that rig's chief of staff, so the column can carry its badge. One
    // flashed character pack serves every column, so the icon is what distinguishes them.
    const agent = rig.agent && /^[a-z0-9-]+$/.test(rig.agent) ? `:${rig.agent}` : '';
    return `${label}:${state}${agent}`;
  }).join(',');
}

export interface RigView {
  reachable: boolean;
  rigs: RigCharacter[];
}

export interface RigCoordinatorOptions {
  // One snapshot of every rig, or a rejection when the OpenRig daemon is unreachable.
  poll: () => Promise<RigSnapshot[]>;
  onChange: (view: RigView) => void;
  matcher?: CosMatcher;
  sweepMs?: number;
}

export class RigCoordinator {
  readonly #poll: () => Promise<RigSnapshot[]>;
  readonly #onChange: (view: RigView) => void;
  readonly #matcher: CosMatcher | undefined;
  #current: RigView | null = null;
  #polling = false;
  #timer: NodeJS.Timeout | undefined;

  constructor(options: RigCoordinatorOptions) {
    this.#poll = options.poll;
    this.#onChange = options.onChange;
    this.#matcher = options.matcher;
    const sweepMs = options.sweepMs ?? 5000;
    if (sweepMs > 0) this.#timer = setInterval(() => void this.pollOnce(), sweepMs);
  }

  get view(): RigView | null {
    return this.#current;
  }

  async pollOnce(): Promise<RigView> {
    if (this.#polling && this.#current) return this.#current;
    this.#polling = true;
    try {
      const snapshots = await this.#poll();
      return this.#publish({
        reachable: true,
        rigs: snapshots.map(snapshot => foldRigCharacter(snapshot, this.#matcher)),
      });
    } catch {
      // OpenRig away is a view, not a fault: the fleet panel shows nothing, honestly.
      return this.#publish({reachable: false, rigs: []});
    } finally {
      this.#polling = false;
    }
  }

  #publish(view: RigView): RigView {
    const changed = !this.#current || JSON.stringify(this.#current) !== JSON.stringify(view);
    this.#current = view;
    if (changed) this.#onChange(view);
    return view;
  }

  close(): void {
    if (this.#timer) clearInterval(this.#timer);
  }
}
