'use strict';

const BridgeDriver = require('../../lib/BridgeDriver');

class AreaDriver extends BridgeDriver {

  async onInit() {
    this.modeChangedTrigger = this.homey.flow.getDeviceTriggerCard('area_mode_changed');

    this.homey.flow.getConditionCard('area_mode_is')
      .registerRunListener(async ({ device, mode }) => device.getCapabilityValue('paradox_area_mode') === mode);
    this.homey.flow.getConditionCard('area_is_ready')
      .registerRunListener(async ({ device }) => device.getCapabilityValue('paradox_ready') === true);

    this.homey.flow.getActionCard('area_set_mode')
      .registerRunListener(async ({ device, mode }) => device.setMode(mode));
    this.homey.flow.getActionCard('area_set_mode_as')
      .registerRunListener(async ({ device, mode, who }) => device.setMode(mode, who));
    this.homey.flow.getActionCard('area_beep')
      .registerRunListener(async ({ device }) => device.sendAction({ action: 'beep' }));
    this.homey.flow.getActionCard('area_pgm')
      .registerRunListener(async ({ device, pgm, state }) => device.sendAction({
        action: state === 'on' ? 'pgm_on' : 'pgm_off',
        pgm: Math.round(pgm),
      }));
  }

  triggerModeChanged(device, mode) {
    this.modeChangedTrigger.trigger(device, { mode }).catch(this.error);
  }

  devicesFromStatus(status) {
    return (status.partitions || [])
      .filter((p) => p.inUse)
      .map((p) => ({
        name: `Paradox ${p.label}`,
        data: { id: `area-${p.id}`, area: p.id },
        settings: { partial_mode: 'stay' },
      }));
  }

}

module.exports = AreaDriver;
