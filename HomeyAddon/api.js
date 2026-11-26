'use strict';

// Web API used by the app settings page (settings/index.html).
module.exports = {

  // Current connection, its status and the bridges found via mDNS.
  async getState({ homey }) {
    return homey.app.getState();
  },

  // Try a connection without saving it: { host, username, password } (empty password = saved one).
  async testConnection({ homey, body }) {
    return homey.app.testConnection(body || {});
  },

};
