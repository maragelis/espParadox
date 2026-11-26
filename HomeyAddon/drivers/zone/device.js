'use strict';

const BridgeDevice = require('../../lib/BridgeDevice');

/** One alarm zone: contact or motion sensor (follows the bridge's sensor type). */
class ZoneDevice extends BridgeDevice {

  async onInit() {
    this.zone = this.getData().zone;
    await super.onInit();
  }

  async onStatus(status) {
    const z = (status.zones || []).find((x) => x.id === this.zone);
    if (!z) {
      await this.setUnavailable(`Zone ${this.zone} not found on the bridge`);
      return;
    }
    if (!z.enabled) {
      await this.setUnavailable('Zone is disabled on the bridge');
      return;
    }
    await this.setAvailable();

    // Sensor type changed on the bridge's Zones page: swap the capability.
    const want = z.sensor === 'motion' ? 'alarm_motion' : 'alarm_contact';
    const other = want === 'alarm_motion' ? 'alarm_contact' : 'alarm_motion';
    if (!this.hasCapability(want)) {
      await this.addCapability(want);
      if (this.hasCapability(other)) await this.removeCapability(other);
    }

    await this.updateCapability(want, Boolean(z.open));
    await this.updateCapability('alarm_tamper', Boolean(z.tamper));
    await this.updateCapability('alarm_battery', Boolean(z.lowBattery));
    await this.updateCapability('paradox_bypassed', Boolean(z.bypassed));
  }

}

module.exports = ZoneDevice;
