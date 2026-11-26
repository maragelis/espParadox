'use strict';

const BridgeDevice = require('../../lib/BridgeDevice');

const MODE_ACTIONS = {
  disarmed: 'disarm',
  stay: 'arm_stay',
  sleep: 'arm_sleep',
  full: 'arm_full',
};

const MODE_NAMES = { stay: 'Stay', sleep: 'Sleep', full: 'Full' };

/** One alarm area (partition) of the panel. */
class AreaDevice extends BridgeDevice {

  async onInit() {
    this.area = this.getData().area;
    if (!this.hasCapability('paradox_ac_failure')) await this.addCapability('paradox_ac_failure'); // devices paired before 1.4.0
    if (!this.hasCapability('paradox_battery_low')) await this.addCapability('paradox_battery_low'); // before 1.5.0
    this.lastMode = null;

    // Homey's standard alarm control: armed / partially_armed / disarmed.
    // Can be turned off in the device settings, so only "Alarm mode" controls the panel.
    this.registerCapabilityListener('homealarm_state', async (value) => {
      if (!this.getSetting('homealarm_control')) {
        // Throwing makes Homey reject the change; the next status poll shows the real state again.
        throw new Error('Arming/disarming from the Home alarm tile is turned off - use Alarm mode');
      }
      if (value === 'armed') return this.setMode('full');
      if (value === 'partially_armed') return this.setMode(this.getSetting('partial_mode') || 'stay');
      return this.setMode('disarmed');
    });
    // The panel's own modes (picker).
    this.registerCapabilityListener('paradox_area_mode', async (value) => this.setMode(value));

    await this._applyHomealarmControl(Boolean(this.getSetting('homealarm_control')));
    await super.onInit();
  }

  async onSettings({ newSettings, changedKeys }) {
    if (changedKeys.includes('homealarm_control')) {
      await this._applyHomealarmControl(Boolean(newSettings.homealarm_control));
    }
  }

  /**
   * Also mark the Home alarm tile read-only when control is off, so Homey shows it
   * as a status. Best effort: if this Homey version doesn't allow it, the listener
   * above still refuses every change.
   */
  async _applyHomealarmControl(allowed) {
    try {
      await this.setCapabilityOptions('homealarm_state', { setable: allowed });
    } catch (err) {
      this.log(`Could not set homealarm_state setable=${allowed}: ${err.message}`);
    }
  }

  /** who: optional name for the timeline ("Set alarm mode ... as ..." flow card). */
  async setMode(mode, who) {
    const action = MODE_ACTIONS[mode];
    if (!action) throw new Error('Choose disarmed, stay, sleep or full');
    this.homey.app.noteHomeyCommand(this.area, who); // timeline: "by Homey" / "by <who> (Homey)"
    await this.sendAction({ action, partition: this.area });
  }

  /**
   * Timeline entry for a mode change. Waits a moment so the panel's
   * "armed by ..." / "zone in alarm" event (same or next poll) is known.
   */
  _timeline(mode, previous) {
    this.homey.setTimeout(() => {
      const app = this.homey.app;
      const name = this.getName();
      let text;
      if (mode === 'alarm') {
        const cause = app.takeAlarmCause(this.area);
        text = `**${name}**: ALARM${cause ? ` - ${cause}` : ''}`;
      } else {
        const who = app.takeActor(this.area);
        const by = who ? ` by **${who}**` : '';
        if (mode === 'disarmed') text = `**${name}** disarmed${previous === 'alarm' ? ' after alarm' : ''}${by}`;
        else text = `**${name}** armed: ${MODE_NAMES[mode] || mode}${by}`;
      }
      app.addTimeline(text).catch(this.error);
    }, 3000);
  }

  async onStatus(status) {
    const p = (status.partitions || []).find((x) => x.id === this.area);
    if (!p) {
      await this.setUnavailable(`Area ${this.area} not found on the bridge`);
      return;
    }
    await this.setAvailable();

    const mode = p.alarm ? 'alarm' : p.mode; // disarmed | stay | sleep | full | alarm
    let homeState = 'disarmed';
    if (p.armed) homeState = p.mode === 'full' ? 'armed' : 'partially_armed';

    await this.updateCapability('homealarm_state', homeState);
    await this.updateCapability('paradox_area_mode', mode);
    await this.updateCapability('alarm_generic', Boolean(p.alarm));
    await this.updateCapability('paradox_ready', Boolean(p.ready));
    await this.updateCapability('paradox_ac_failure', Boolean(status.panel && status.panel.acFailure));
    await this.updateCapability('paradox_battery_low', Boolean(status.panel && status.panel.batteryLow));

    // Track the real panel mode separately from the capability, which Homey sets
    // optimistically when the user picks a mode.
    if (this.lastMode !== null && this.lastMode !== mode) {
      this.driver.triggerModeChanged(this, mode);
      this._timeline(mode, this.lastMode);
    }
    this.lastMode = mode;
  }

}

module.exports = AreaDevice;
