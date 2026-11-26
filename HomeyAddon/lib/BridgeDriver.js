'use strict';

const Homey = require('homey');
const BridgeClient = require('./BridgeClient');

/**
 * Shared pairing. The "bridge" view first asks 'check': if the app can already
 * reach the bridge (app settings or mDNS), it goes straight to the device list.
 * Otherwise it shows a form (pre-filled with a bridge found via mDNS); 'connect'
 * tests it and saves it to the app settings, so every device uses it.
 */
class BridgeDriver extends Homey.Driver {

  async onPair(session) {
    let status = null;

    session.setHandler('check', async () => {
      const app = this.homey.app;
      const c = app.connection();
      try {
        status = await app.getBridge().getStatus();
        return { ok: true, host: c.host, source: c.source, version: status.version };
      } catch (err) {
        return {
          ok: false,
          error: err.message,
          host: app.homey.settings.get('host') || '',
          username: c.username,
          passwordSet: Boolean(app.homey.settings.get('password')),
          discovered: app.discovered(),
        };
      }
    });

    session.setHandler('connect', async (data) => {
      const app = this.homey.app;
      const host = String((data && data.host) || '').trim();
      const username = String((data && data.username) || '').trim() || 'admin';
      const password = (data && data.password) || app.homey.settings.get('password') || '';
      const target = host || app.connection().host;
      if (!target) throw new Error('Enter the bridge address');
      status = await new BridgeClient(this.homey, { host: target, username, password }).getStatus();
      if (!status || !status.link) throw new Error('That address is not a Paradox bridge');
      // Save to the app settings - all devices share this connection.
      app.homey.settings.set('host', host); // empty = keep finding it via mDNS
      app.homey.settings.set('username', username);
      app.homey.settings.set('password', password);
      return { version: status.version || '?', link: status.link.state };
    });

    session.setHandler('list_devices', async () => {
      if (!status) status = await this.homey.app.getBridge().getStatus();
      return this.devicesFromStatus(status);
    });
  }

  // eslint-disable-next-line no-unused-vars
  devicesFromStatus(status) {
    return [];
  }

}

module.exports = BridgeDriver;
