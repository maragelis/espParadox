'use strict';

const Homey = require('homey');

/**
 * Base for area and zone devices: listens to the app's shared BridgeClient
 * (configured in the app settings), handles availability, and hands each
 * status poll to onStatus().
 */
class BridgeDevice extends Homey.Device {

  async onInit() {
    this.client = this.homey.app.getBridge();
    this._onStatus = (status) => this._handleStatus(status).catch(this.error);
    this._onUnavailable = (err) => this.setUnavailable(`Bridge unreachable: ${err.message}`).catch(this.error);
    this.client.on('status', this._onStatus);
    this.client.on('unavailable', this._onUnavailable);
    // The shared client may already know the state: apply it right away.
    if (this.client.reachable === false) this._onUnavailable(new Error(this.client.lastError || 'no answer'));
    else if (this.client.status) this._onStatus(this.client.status);
  }

  async onUninit() {
    this._detach();
  }

  async onDeleted() {
    this._detach();
  }

  _detach() {
    if (!this.client) return;
    this.client.removeListener('status', this._onStatus);
    this.client.removeListener('unavailable', this._onUnavailable);
    this.client = null;
  }

  async _handleStatus(status) {
    const link = status.link || {};
    if (link.state !== 'online') {
      const why = link.stopped ? 'Panel disconnected on the bridge' : `Panel ${link.state || 'offline'}`;
      await this.setUnavailable(why);
      return;
    }
    await this.onStatus(status);
  }

  // eslint-disable-next-line no-unused-vars
  async onStatus(status) {}

  /** Sets a capability only if present and changed (avoids flooding Insights/flows). */
  async updateCapability(capability, value) {
    if (!this.hasCapability(capability)) return;
    if (this.getCapabilityValue(capability) === value) return;
    await this.setCapabilityValue(capability, value);
  }

  async sendAction(params) {
    if (!this.client) throw new Error('Not connected to the bridge');
    await this.client.action(params);
  }

}

module.exports = BridgeDevice;
