'use strict';

const Homey = require('homey');
const BridgeClient = require('./lib/BridgeClient');

const CONNECTION_KEYS = ['host', 'username', 'password'];

const ACTOR_MAX_AGE_MS = 90 * 1000; // an event / command explains a change this long afterwards
const VIA_BRIDGE = Symbol('bridge'); // panel logged a "software" (WinLoad-type) command = the bridge

// Special arming / disarming sub-groups (event groups 30 and 34) -> who/what did it.
const SPECIAL_ARM = {
  0: 'auto-arm', 1: 'auto-arm (late to close)', 2: 'auto-arm (no movement)', 3: 'partial arming', 4: 'quick arm', 5: VIA_BRIDGE, 6: 'keyswitch',
};
const SPECIAL_DISARM = {
  0: 'auto-arm cancelled', 1: VIA_BRIDGE, 2: VIA_BRIDGE, 3: VIA_BRIDGE, 4: 'paramedic reset', 5: 'keyswitch', 6: 'keyswitch', 7: 'keyswitch',
};

/**
 * One bridge per app. The connection is configured once in the app settings
 * (Apps -> Paradox SP/MG Bridge -> Configure). If no address is set, the bridge is
 * found via mDNS (_paradox._tcp, advertised by bridge firmware 1.6.9+), and a
 * changed IP address is picked up automatically.
 */
class ParadoxBridgeApp extends Homey.App {

  async onInit() {
    this.panelEventTrigger = this.homey.flow.getTriggerCard('panel_event');

    this.discovery = this.homey.discovery.getStrategy('paradox');
    this.discovery.on('result', (result) => this._onDiscoveryResult(result));
    for (const result of Object.values(this.discovery.getDiscoveryResults())) this._watchResult(result);

    this.actors = new Map(); // area (1/2) or 'any' -> { who, at }
    this.alarmCauses = new Map(); // area or 'any' -> { text, at }
    this.homeyCommands = new Map(); // area -> { at, who } of Homey's last arm/disarm command

    this.client = new BridgeClient(this.homey, this.connection());
    this.client.on('panelEvent', (e) => {
      this._recordEvent(e);
      this.panelEventTrigger.trigger({
        text: e.text || '',
        group: Number(e.group) || 0,
        sub: Number(e.sub) || 0,
        area: (Number(e.partition) || 0) + 1,
        time: e.time || '',
      }).catch(this.error);
    });
    // Panel troubles watched for flows + timeline: AC (mains) power and battery.
    this.troubles = {
      ac: {
        field: 'acFailure', value: null,
        on: this.homey.flow.getTriggerCard('ac_lost'),
        off: this.homey.flow.getTriggerCard('ac_restored'),
        onText: '**Paradox panel**: AC power lost - running on battery',
        offText: '**Paradox panel**: AC power restored',
      },
      battery: {
        field: 'batteryLow', value: null,
        on: this.homey.flow.getTriggerCard('battery_low'),
        off: this.homey.flow.getTriggerCard('battery_ok'),
        onText: '**Paradox panel**: battery low or missing',
        offText: '**Paradox panel**: battery OK again',
      },
    };
    this.homey.flow.getConditionCard('ac_ok').registerRunListener(async () => this.troubles.ac.value === false);
    this.homey.flow.getConditionCard('battery_ok').registerRunListener(async () => this.troubles.battery.value === false);
    this.client.on('status', (status) => this._checkTroubles(status));

    this.client.on('unavailable', (err) => this.log(`Bridge unreachable: ${err.message}`));
    this.client.on('available', () => this.log(`Bridge reachable at ${this.client.host}`));
    this.client.start();

    this.homey.settings.on('set', (key) => {
      if (CONNECTION_KEYS.includes(key)) this.reconfigure();
    });

    this.log(`Paradox SP/MG Bridge app started (${this.client.host || 'no bridge yet'})`);
  }

  // ---- connection --------------------------------------------------------------

  /** Effective connection: the saved address, or the bridge found via mDNS. */
  connection() {
    const saved = (this.homey.settings.get('host') || '').trim();
    const found = this.discovered()[0];
    return {
      host: saved || (found ? found.host : ''),
      username: this.homey.settings.get('username') || 'admin',
      password: this.homey.settings.get('password') || '',
      source: saved ? 'manual' : (found ? 'mdns' : 'none'),
    };
  }

  reconfigure() {
    const c = this.connection();
    if (this.client.host === c.host && this.client.username === c.username && this.client.password === c.password) return;
    this.log(`Bridge connection -> ${c.host || '(none)'} (${c.source})`);
    this.client.configure(c);
  }

  /** Shared client for all devices. */
  getBridge() {
    return this.client;
  }

  // ---- timeline: who changed the alarm --------------------------------------------

  /** Remember who armed / disarmed and what caused an alarm, from the panel's live events. */
  _recordEvent(e) {
    const group = Number(e.group);
    const sub = Number(e.sub);
    const partition = Number(e.partition);
    const area = partition === 0 || partition === 1 ? partition + 1 : null;
    const now = Date.now();
    let who;
    if ([29, 31, 32, 33].includes(group)) who = e.user || `user ${sub}`; // armed / disarmed / cancelled by user
    else if (group === 30) who = SPECIAL_ARM[sub];
    else if (group === 34) who = SPECIAL_DISARM[sub];
    if (who !== undefined) {
      if (area) this.actors.set(area, { who, at: now });
      this.actors.set('any', { who, at: now });
    }
    if ([36, 37, 40].includes(group)) { // zone alarm, fire alarm, special alarm (panic, duress...)
      if (area) this.alarmCauses.set(area, { text: e.text, at: now });
      this.alarmCauses.set('any', { text: e.text, at: now });
    }
  }

  /**
   * Called by an area device right before Homey sends it an arm / disarm command.
   * Homey doesn't tell apps which user tapped a device, so `who` is only known
   * when a flow passes it ("Set alarm mode ... as [who]").
   */
  noteHomeyCommand(area, who) {
    this.homeyCommands.set(area, { at: Date.now(), who: String(who || '').trim() });
  }

  _recent(map, key) {
    const v = map.get(key);
    return v && Date.now() - (v.at || v) < ACTOR_MAX_AGE_MS ? v : null;
  }

  /** Who caused the latest arm / disarm of this area (consumed once), or null. */
  takeActor(area) {
    const entry = this._recent(this.actors, area) || this._recent(this.actors, 'any');
    const homeySent = this._recent(this.homeyCommands, area);
    this.actors.delete(area);
    this.actors.delete('any');
    this.homeyCommands.delete(area);
    // Homey's own command wins: with a user-code login the panel reports it as the
    // bridge's panel user (e.g. "Master 1"), which isn't who did it.
    if (homeySent) return homeySent.who ? `${homeySent.who} (Homey)` : 'Homey';
    if (entry && entry.who !== VIA_BRIDGE) return entry.who;
    if (entry) return 'the bridge (web page / MQTT)';
    return null;
  }

  /** What set off the alarm in this area (consumed once), or null. */
  takeAlarmCause(area) {
    const entry = this._recent(this.alarmCauses, area) || this._recent(this.alarmCauses, 'any');
    this.alarmCauses.delete(area);
    this.alarmCauses.delete('any');
    return entry ? entry.text : null;
  }

  /** Adds an entry to the Homey timeline (unless switched off in the app settings). */
  async addTimeline(text) {
    if (this.homey.settings.get('timeline') === false) return;
    await this.homey.notifications.createNotification({ excerpt: text });
  }

  _checkTroubles(status) {
    if (!status.link || status.link.state !== 'online' || !status.link.statusComplete || !status.panel) return;
    for (const [name, t] of Object.entries(this.troubles)) {
      const active = Boolean(status.panel[t.field]);
      const previous = t.value;
      t.value = active;
      if (previous === null || previous === active) continue; // first reading or no change
      this.log(`Panel ${name} trouble ${active ? 'on' : 'off'}`);
      (active ? t.on : t.off).trigger().catch(this.error);
      this.addTimeline(active ? t.onText : t.offText).catch(this.error);
    }
  }

  // ---- mDNS ----------------------------------------------------------------------

  discovered() {
    return Object.values(this.discovery.getDiscoveryResults()).map((r) => ({
      id: r.id,
      host: r.port && Number(r.port) !== 80 ? `${r.address}:${r.port}` : r.address,
      address: r.address,
      name: r.host || r.name || r.address,
      version: (r.txt && r.txt.version) || '',
    }));
  }

  _onDiscoveryResult(result) {
    this.log(`Found bridge ${result.id} at ${result.address}`);
    this._watchResult(result);
    this.reconfigure();
  }

  _watchResult(result) {
    result.on('addressChanged', () => {
      this.log(`Bridge ${result.id} moved to ${result.address}`);
      this.reconfigure();
    });
  }

  // ---- settings page API --------------------------------------------------------------

  getState() {
    const c = this.connection();
    const s = this.client.status;
    return {
      host: this.homey.settings.get('host') || '',
      username: c.username,
      passwordSet: Boolean(this.homey.settings.get('password')),
      effectiveHost: c.host,
      source: c.source,
      reachable: this.client.reachable,
      bridgeVersion: s ? s.version : null,
      panelLink: s && s.link ? s.link.state : null,
      lastError: this.client.lastError || null,
      timeline: this.homey.settings.get('timeline') !== false,
      discovered: this.discovered(),
    };
  }

  async testConnection({ host, username, password }) {
    const c = this.connection();
    const target = {
      host: String(host || '').trim() || c.host,
      username: String(username || '').trim() || c.username,
      password: password || c.password,
    };
    if (!target.host) throw new Error('No bridge address and no bridge found on the network');
    const status = await new BridgeClient(this.homey, target).getStatus();
    if (!status || !status.link) throw new Error('That address is not a Paradox bridge');
    return { host: target.host, version: status.version, panelLink: status.link.state };
  }

}

module.exports = ParadoxBridgeApp;
