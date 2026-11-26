'use strict';

const http = require('http');
const { EventEmitter } = require('events');

const POLL_MS = 2000;
const TIMEOUT_MS = 5000;

/**
 * Talks to the ESP32 Paradox bridge over its HTTP API (basic auth):
 *   GET  /api/status   everything as JSON
 *   POST /api/action   action=..., partition=, zone=, pgm=
 *
 * Events:
 *   'status'      (status)        every successful poll
 *   'panelEvent'  (event)         each new live panel event, oldest first
 *   'available'   ()              bridge reachable again
 *   'unavailable' (error)         bridge unreachable / bad login
 */
class BridgeClient extends EventEmitter {

  constructor(homey, { host, username, password }) {
    super();
    this.homey = homey;
    this.host = String(host || '').trim();
    this.username = username || '';
    this.password = password || '';
    this.status = null;
    this.reachable = null;
    this.lastError = null;
    this._timer = null;
    this._lastEventSig = null;
    this._polling = false;
    this.setMaxListeners(100); // one listener per device
  }

  get key() {
    return BridgeClient.key({ host: this.host, username: this.username, password: this.password });
  }

  static key({ host, username, password }) {
    return `${String(host || '').trim()}|${username || ''}|${password || ''}`;
  }

  /** Point the client at a different bridge / login; polls again right away. */
  configure({ host, username, password }) {
    this.host = String(host || '').trim();
    this.username = username || '';
    this.password = password || '';
    this.status = null;
    this.reachable = null;
    this._lastEventSig = null;
    if (this._timer) this._poll();
  }

  start() {
    if (this._timer) return;
    this._poll();
    this._timer = this.homey.setInterval(() => this._poll(), POLL_MS);
  }

  stop() {
    if (this._timer) this.homey.clearInterval(this._timer);
    this._timer = null;
  }

  async getStatus() {
    return this._request('GET', '/api/status');
  }

  /** Sends a command; throws with the bridge's message if it is refused. */
  async action(params) {
    const res = await this._request('POST', '/api/action', params);
    if (!res || !res.ok) throw new Error((res && res.message) || 'Bridge refused the command');
    return res;
  }

  async _poll() {
    if (this._polling) return; // previous request still running
    this._polling = true;
    try {
      const status = await this.getStatus();
      this.status = status;
      this.lastError = null;
      if (this.reachable !== true) {
        this.reachable = true;
        this.emit('available');
      }
      this._emitNewEvents(status.events || []);
      this.emit('status', status);
    } catch (err) {
      this.lastError = err.message;
      if (this.reachable !== false) {
        this.reachable = false;
        this.emit('unavailable', err);
      }
    } finally {
      this._polling = false;
    }
  }

  // status.events is newest first. Emit the ones we haven't seen, oldest first.
  _emitNewEvents(events) {
    const sig = (e) => `${e.time}|${e.group}|${e.sub}|${e.partition}|${e.text}`;
    if (!events.length) return;
    if (this._lastEventSig === null) { // first poll: don't replay history
      this._lastEventSig = sig(events[0]);
      return;
    }
    const fresh = [];
    for (const e of events) {
      if (sig(e) === this._lastEventSig) break;
      fresh.push(e);
    }
    if (!fresh.length) return;
    this._lastEventSig = sig(events[0]);
    for (const e of fresh.reverse()) this.emit('panelEvent', e);
  }

  _request(method, path, params) {
    if (!this.host) return Promise.reject(new Error('No bridge configured - open the app settings'));
    return new Promise((resolve, reject) => {
      const body = params ? new URLSearchParams(params).toString() : null;
      const [hostname, port] = this.host.split(':');
      const headers = {
        Authorization: `Basic ${Buffer.from(`${this.username}:${this.password}`).toString('base64')}`,
      };
      if (body !== null) {
        headers['Content-Type'] = 'application/x-www-form-urlencoded';
        headers['Content-Length'] = Buffer.byteLength(body);
      }
      const req = http.request({
        hostname, port: port ? Number(port) : 80, path, method, headers, timeout: TIMEOUT_MS,
      }, (res) => {
        let data = '';
        res.setEncoding('utf8');
        res.on('data', (chunk) => { data += chunk; });
        res.on('end', () => {
          if (res.statusCode === 401) return reject(new Error('Wrong bridge user or password'));
          let json;
          try {
            json = JSON.parse(data.replace(/^﻿/, ''));
          } catch (e) {
            return reject(new Error(`Unexpected answer from the bridge (HTTP ${res.statusCode})`));
          }
          // /api/action answers 400 with {ok:false,message} - let the caller see the message.
          if (res.statusCode >= 500) return reject(new Error(`Bridge error (HTTP ${res.statusCode})`));
          return resolve(json);
        });
      });
      req.on('timeout', () => req.destroy(new Error(`No answer from the bridge at ${this.host}`)));
      req.on('error', reject);
      if (body !== null) req.write(body);
      req.end();
    });
  }

}

module.exports = BridgeClient;
