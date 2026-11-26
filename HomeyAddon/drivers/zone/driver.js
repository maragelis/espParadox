'use strict';

const BridgeDriver = require('../../lib/BridgeDriver');

const sensorCapability = (sensor) => (sensor === 'motion' ? 'alarm_motion' : 'alarm_contact');

class ZoneDriver extends BridgeDriver {

  async onInit() {
    this.homey.flow.getConditionCard('zone_is_bypassed')
      .registerRunListener(async ({ device }) => device.getCapabilityValue('paradox_bypassed') === true);
    this.homey.flow.getActionCard('zone_toggle_bypass')
      .registerRunListener(async ({ device }) => device.sendAction({ action: 'bypass', zone: device.zone }));
  }

  // Only the zones enabled on the bridge; motion or contact as set on its Zones page.
  devicesFromStatus(status) {
    return (status.zones || [])
      .filter((z) => z.enabled)
      .map((z) => ({
        name: z.label,
        data: { id: `zone-${z.id}`, zone: z.id },
        capabilities: [sensorCapability(z.sensor), 'alarm_tamper', 'alarm_battery', 'paradox_bypassed'],
      }));
  }

}

ZoneDriver.sensorCapability = sensorCapability;
module.exports = ZoneDriver;
