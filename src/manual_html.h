#pragma once
#include <Arduino.h>

#include "version.h"

// User manual, served at /manual WITHOUT login (it holds no settings or secrets, and
// it must be readable when the password is forgotten).
static const char kManualHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bridge Manual</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mute:#5f6773;--line:#e3e6ea;--info:#2563eb;--bad:#b91c1c;--warnbg:#fff7e6;--warnline:#f0c36d;--dangerbg:#fdecec;--dangerline:#f0a3a3}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1a1f26;--fg:#e6e9ee;--mute:#9aa3af;--line:#2a313b;--warnbg:#2a2213;--warnline:#7a5a1c;--dangerbg:#2b1717;--dangerline:#7a2d2d;--bad:#f87171}}
*{box-sizing:border-box}html{scroll-padding-top:70px}
body{margin:0;font:15px/1.6 system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--bg);color:var(--fg)}
header{position:sticky;top:env(safe-area-inset-top,0px);z-index:5;display:flex;flex-wrap:wrap;gap:6px 16px;align-items:center;padding:10px 16px;background:var(--card);border-bottom:1px solid var(--line)}
header h1{font-size:17px;margin:0}header a{color:var(--info)}.ver{font-size:12px;font-weight:500;color:var(--mute)}
main{max-width:860px;margin:auto;padding:16px}
nav.toc{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 16px;margin-bottom:16px}
nav.toc ol{margin:4px 0 0 18px;padding:0;columns:2;column-gap:28px}@media (max-width:600px){nav.toc ol{columns:1}}
nav.toc a{color:var(--info);text-decoration:none}nav.toc a:hover{text-decoration:underline}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:4px 18px 14px;margin-bottom:16px}
h2{font-size:19px;margin:14px 0 8px}h3{font-size:15px;margin:16px 0 4px}
p{margin:6px 0}ul,ol{margin:6px 0 6px 20px;padding:0}li{margin:3px 0}
code,kbd{font:13px ui-monospace,Consolas,monospace;background:var(--bg);border:1px solid var(--line);border-radius:4px;padding:0 4px}
.tw{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:14px;margin:6px 0}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--mute);font-weight:600}
.warn,.danger{border-radius:8px;padding:8px 12px;margin:10px 0}
.warn{background:var(--warnbg);border:1px solid var(--warnline)}.danger{background:var(--dangerbg);border:1px solid var(--dangerline)}
.warn b:first-child,.danger b:first-child{display:block}
.mute{color:var(--mute)}
</style></head><body>
<header><h1>Bridge manual <span class="ver">v)HTML" FW_VERSION R"HTML(</span></h1><a href="/">Panel</a><a href="/settings">Settings</a></header>
<main>

<nav class="toc"><b>Contents</b><ol>
<li><a href="#safety">Important - read first</a></li>
<li><a href="#what">What the bridge does</a></li>
<li><a href="#wiring">Wiring and power</a></li>
<li><a href="#first">First start</a></li>
<li><a href="#login">Connecting to the panel</a></li>
<li><a href="#main">The main page</a></li>
<li><a href="#display">Display and buttons</a></li>
<li><a href="#settings">Settings, tab by tab</a></li>
<li><a href="#zones">Zones</a></li>
<li><a href="#smarthome">Smart home: MQTT or HomeKit</a></li>
<li><a href="#homey">Homey app</a></li>
<li><a href="#babyware">BabyWare through the bridge</a></li>
<li><a href="#updates">Firmware updates and backups</a></li>
<li><a href="#recovery">Forgot the password / can't reach the bridge</a></li>
<li><a href="#trouble">Troubleshooting</a></li>
<li><a href="#limits">Known limitations</a></li>
<li><a href="#console">USB console commands</a></li>
<li><a href="#ref">Addresses and ports</a></li>
</ol></nav>

<section id="safety"><h2>1. Important - read first</h2>
<div class="danger"><b>This bridge is a convenience add-on, not part of your alarm system's certification.</b>
Your panel keeps protecting the house on its own. Don't rely on Homey, Home Assistant, HomeKit or the bridge as the only way an alarm is noticed - keep the panel's own siren and monitoring (central station, phone dialer) as they are.</div>
<ul>
<li><b>The panel's programming is never changed.</b> The bridge only reads the panel's memory and sends the panel's normal commands (arm, disarm, bypass, PGM, beep, clock). It cannot write zone, code or communicator settings.</li>
<li><b>Change the factory web password</b> (<code>admin</code> / <code>changeme</code>) on <i>Settings &rarr; Security</i> right away. Anyone who can open the web page can disarm the alarm.</li>
<li><b>Keep the bridge on your home network only.</b> It uses plain HTTP (no encryption). Never forward its port on your router to the internet - use a VPN (or Homey / HomeKit remote access) to control it from outside.</li>
<li><b>Backups contain all passwords</b> (WiFi, web login, MQTT, PC password / user code) in plain text. Store them privately.</li>
<li><b>Home Assistant can disarm without a code</b> through MQTT. Limit who can use your Home Assistant dashboards, or untick <i>Allow control over MQTT</i>.</li>
<li><b>Protect your MQTT broker with a password</b> - anyone who can publish to it can send arm / disarm commands.</li>
<li><b>Wiring:</b> the panel's serial port is 5&nbsp;V; the ESP32 is 3.3&nbsp;V. A voltage divider on the RX line is required (see <a href="#wiring">Wiring</a>), otherwise the board can be damaged.</li>
</ul></section>

<section id="what"><h2>2. What the bridge does</h2>
<p>An ESP32 (TTGO T-Display or any ESP32 board) plugs into the serial connector of a Paradox <b>Spectra SP</b> or <b>Magellan MG</b> panel and makes the panel available on your home network:</p>
<ul>
<li>A <b>web page</b>: arm / disarm areas, see zones, troubles and live events, bypass zones, switch PGM outputs.</li>
<li>A <b>Homey app</b> (Paradox SP/MG Bridge), <b>MQTT</b> for Home Assistant / Homebridge / Homey, or built-in <b>Apple HomeKit</b>.</li>
<li>Status on the board's <b>display</b>.</li>
</ul>
<p>Everything works locally - no cloud service is involved.</p></section>

<section id="wiring"><h2>3. Wiring and power</h2>
<div class="tw"><table><tr><th>Panel serial connector</th><th>ESP32</th></tr>
<tr><td>TX (5 V signal)</td><td>through a <b>voltage divider</b> (1&nbsp;k&Omega; in series, 2&nbsp;k&Omega; to GND) to the RX pin (factory: GPIO27)</td></tr>
<tr><td>RX</td><td>TX pin (factory: GPIO26) - the 3.3 V signal is read fine by the panel</td></tr>
<tr><td>GND</td><td>GND</td></tr></table></div>
<ul>
<li>The pins can be changed on <i>Settings &rarr; System &rarr; Panel connection pins</i>. Avoid GPIO12 - the board may not start with the panel connected to it (that's why it isn't offered).</li>
<li><b>Power:</b> USB 5 V, or the panel's AUX output through a 12 V &rarr; 5 V step-down converter. Don't connect the panel's 12-13.8 V directly to the board.</li>
<li>Only one program can use the panel's serial port. To use BabyWare / WinLoad on it, press <b>Disconnect panel</b> on the main page first (or unplug the bridge).</li>
</ul></section>

<section id="first"><h2>4. First start</h2>
<ol>
<li>Power the bridge. If it doesn't know a WiFi network (or can't join it for 30 seconds) it opens its own WiFi: <b>ParadoxBridge</b> (factory password <code>paradox123</code>).</li>
<li>Connect to it and open <code>http://192.168.4.1</code>. Log in with <code>admin</code> / <code>changeme</code>.</li>
<li><i>Settings &rarr; Network</i>: choose your WiFi (<b>Scan</b> lists networks), enter its password, press <b>Save</b>. The bridge reboots and joins your network; the display shows its new address. It's also reachable as <code>http://paradox-<code>http://paradox.local</code>.lt;last number of its IP<code>http://paradox.local</code>.gt;.local</code> (e.g. <code>paradox-84.local</code> at 192.168.2.84), so two bridges on one network never clash.</li>
<li><i>Settings &rarr; Security</i>: change the password.</li>
<li><i>Settings &rarr; Panel</i>: choose the login method (next section) and Save.</li>
<li><i>System &rarr; Backup</i>: download a backup once everything works.</li>
</ol></section>

<section id="login"><h2>5. Connecting to the panel</h2>
<p>The bridge logs in to the panel with one of two methods (<i>Settings &rarr; Panel &rarr; Login method</i>):</p>
<div class="tw"><table><tr><th></th><th>PC password [911]</th><th>User code</th></tr>
<tr><td>Needs</td><td>The 4-character PC password, panel section [911] (factory 0000)</td><td>A keypad user code (4-6 digits)</td></tr>
<tr><td>Access</td><td>The whole panel</td><td>Only the areas that user may use</td></tr>
<tr><td>Panel log</td><td>"Arming via WinLoad"</td><td>Logged as that user</td></tr></table></div>
<ul>
<li><b>Finding [911]:</b> use the <i>How do I find the PC password?</i> button on the Panel tab (keypad steps, needs the installer code). It only <i>reads</i> the value - on an LED keypad, press [ENTER] once before typing anything, or you'd change it.</li>
<li><b>Wrong code / password:</b> the bridge tries a few times, then <b>stops</b> and shows the panel's reason (e.g. <i>invalid user code</i>) so it never keeps hammering the panel. Fix the setting, then press <b>Connect panel</b> on the main page.</li>
<li><b>Connect / Disconnect panel</b> (main page) ends or restarts the session. A reboot always reconnects.</li>
</ul></section>

<section id="main"><h2>6. The main page</h2>
<ul>
<li><b>Areas:</b> Full / Stay / Sleep arm and Disarm. An area only arms when it's <i>ready</i> (all zones closed, or bypassed).</li>
<li><b>Panel:</b> model, firmware, panel clock, AC power, battery, raw voltage levels, frame counters (reset every 10 minutes - <i>bad</i> should stay 0; if it counts up, check the wiring).</li>
<li><b>Troubles</b>, <b>Zones</b> (open, alarm, bypassed, tamper, battery, signal), <b>PGM outputs</b> (fold out), <b>Events</b> (last 40).</li>
<li><b>Sync panel clock</b> sets the panel's time from the internet (NTP).</li>
<li>A red banner warns while the factory password is still in use.</li>
</ul></section>

<section id="display"><h2>7. Display and buttons (T-Display)</h2>
<ul>
<li><b>Right button:</b> next page - connection (WiFi, IP, panel, areas), zone grid, recent events, and while HomeKit is on a HomeKit page with the setup QR code and code (scan it with the iPhone camera).</li>
<li><b>Left button (BOOT):</b> short press = backlight on/off. <b>Reset all settings: only at start-up</b> - press within 5 s of power-on and hold 10 s; pressing later never resets (see <a href="#recovery">recovery</a>).</li>
<li>Header colours: green = panel online, orange = connecting, dark red = disconnected, flashing red = <b>alarm</b>.</li>
<li>Other ESP32 boards: untick <i>System &rarr; Board &rarr; TTGO T-Display screen</i>; the BOOT-button reset still works.</li>
</ul></section>

<section id="settings"><h2>8. Settings, tab by tab</h2>
<p>Change anything on any tab, then press the single <b>Save</b> button (top right; it shows <b>Save &bull;</b> when something is unsaved). Bridge settings reboot the bridge; zone changes and the password don't.</p>
<div class="tw"><table>
<tr><th>Tab</th><th>Contents</th></tr>
<tr><td>Network</td><td>WiFi, <b>IP address: DHCP or static</b> (Use current values fills it in), hostname, fallback access point</td></tr>
<tr><td>Panel</td><td>Login method, PC password [911] or user code, read configuration from panel</td></tr>
<tr><td>Zones</td><td>Name, contact / motion, Auto / Always / Never per zone</td></tr>
<tr><td>Smart home</td><td>Nothing / MQTT / Apple HomeKit and their settings</td></tr>
<tr><td>Security</td><td>Web user, change password</td></tr>
<tr><td>System</td><td>Board / display, panel pins, time zone, network firmware upload, reset to defaults, backup / restore, firmware update</td></tr>
</table></div>
<div class="warn"><b>Static IP:</b> pick an address outside your router's DHCP range (or reserve it in the router). A wrong address makes the bridge unreachable - see <a href="#recovery">recovery</a>.</div></section>

<section id="zones"><h2>9. Zones</h2>
<ul>
<li><b>Name on the bridge:</b> leave empty to use the panel's label.</li>
<li><b>Show:</b> <i>Auto</i> follows the panel, <i>Always</i> / <i>Never</i> override it. Hidden zones disappear everywhere (page, display, Homey, MQTT, Home Assistant, HomeKit).</li>
<li><b>Sensor:</b> contact (door / window) or motion - used by Home Assistant, HomeKit and the Homey app.</li>
<li><b>Re-read panel configuration</b> reads the labels again (read only).</li>
</ul>
<div class="warn"><b>On panel models the protocol document doesn't list (e.g. product ID 0x1A) the bridge can't read which zones are programmed or their areas</b> - set unused zones to <i>Never</i>, otherwise all 32 are shown. The main page shows your panel's ID.</div></section>

<section id="smarthome"><h2>10. Smart home: MQTT or HomeKit</h2>
<p>Only one at a time (<i>Settings &rarr; Smart home &rarr; Connect with</i>). The Homey app works with any choice.</p>
<h3>MQTT (Home Assistant, Homebridge, Homey MQTT)</h3>
<ul>
<li>Enter the broker address, port, login and base topic (default <code>paradox</code>). The last number of the bridge's IP address is added, e.g. <code>paradox-84/...</code> at 192.168.2.84, so two bridges on one broker never clash - the MQTT tab shows the topic in use. <b>Use a fixed address</b> (static IP or a DHCP reservation in the router) so the topics and the network name never change.</li>
<li><b>Home Assistant discovery</b> creates a "Paradox alarm" device automatically: alarm panel per area, sensor per zone, panel connection, troubles, AC power, battery. <i>Publish Home Assistant discovery now</i> re-sends it. <i>Find old Home Assistant devices</i> lists Paradox entries left by an earlier integration or a previous bridge board and can delete them (this bridge's own device stays).</li>
<li>Main topics: <code>paradox/area/1/state</code>, commands to <code>paradox/area/1/set</code> (<code>full</code>, <code>stay</code>, <code>sleep</code>, <code>disarm</code>), <code>paradox/zone/N/open</code>, <code>paradox/event</code>, <code>paradox/panel/ac_power</code>, <code>paradox/panel/battery_low</code>.</li>
<li><b>Retain</b> options keep the last zone / panel state on the broker for clients that connect later.</li>
</ul>
<h3>Apple HomeKit</h3>
<ul>
<li>After choosing HomeKit and Save, the tab shows a <b>QR code</b> and <b>setup code</b>. iPhone: Home app &rarr; <b>+</b> &rarr; Add Accessory &rarr; scan. Confirm "Uncertified Accessory" with <i>Add Anyway</i>.</li>
<li>Each area becomes an alarm (Home = Stay, Away = Full, Night = Sleep), each zone a door or motion sensor.</li>
<li><b>AC power</b> and <b>Panel battery</b> appear as contact sensors: <i>closed</i> = OK, <i>open</i> = trouble (mains lost / battery low or missing). Turn on their notifications in the Home app. While either has a trouble, the area alarms show <i>Fault</i>.</li>
<li><b>After changing zones, reboot the bridge</b> - the HomeKit accessory list is built at start.</li>
<li><b>Leaving HomeKit</b> deletes the pairing data on the bridge. Remove "Paradox Alarm" from the Home app too.</li>
</ul></section>

<section id="homey"><h2>11. Homey app</h2>
<ul>
<li>Apps &rarr; <b>Paradox SP/MG Bridge</b> &rarr; Configure: enter the bridge's web login once; the bridge is found automatically (or type its IP).</li>
<li>Devices: <b>Alarm area</b> and <b>Alarm zone</b>. Arm / disarm with <b>Alarm mode</b>; the Home alarm tile is display-only unless you allow it in the device settings.</li>
<li>Timeline entries say who armed / disarmed. Homey doesn't tell apps which Homey user tapped a button - use the flow card <i>Set alarm mode &hellip; as [who]</i> (e.g. with the Name tag of a presence trigger) to record a person.</li>
<li><b>Changed the bridge password?</b> Update it in the app settings too, or the devices become unavailable.</li>
</ul></section>

<section id="babyware"><h2>BabyWare through the bridge</h2>
<p>BabyWare can reach the panel through the bridge in two ways. <b>Easiest: the IP connection</b> - the bridge answers like an IP150 module:</p>
<ol>
<li><b>Bridge:</b> <i>Settings &rarr; Panel &rarr; BabyWare pass-through</i>: set the <b>IP password (BabyWare)</b> (factory <code>paradox</code>), Save. Then choose how long to wait and press <b>Start BabyWare mode</b>.</li>
<li><b>BabyWare:</b> connection type <b>IP150 / Static IP</b>, IP address = the bridge's address, port <code>10000</code> (the pass-through port), IP password = the one above; panel PC password [911] as usual. Connect.</li>
</ol>
<p><b>Or serial</b>, as if a 307USB cable were plugged in:</p>
<ol>
<li><b>PC:</b> install a virtual COM port tool, e.g. <b>HW VSP3</b> (HW group, free) or <b>com0com + hub4com</b>. Create a virtual port (e.g. <code>COM10</code>) that connects to the bridge's IP address and the pass-through port (default <code>10000</code>). Choose <b>RFC 2217</b> / "Telnet COM port control" if the tool offers it (BabyWare can then set the speed); raw TCP works too.</li>
<li><b>Bridge:</b> <i>Settings &rarr; Panel &rarr; BabyWare pass-through</i> &rarr; choose how long to wait for BabyWare &rarr; <b>Start BabyWare mode</b>.</li>
<li><b>BabyWare:</b> connection type <b>Serial / USB direct</b>, port <code>COM10</code>, <b>9600 baud</b>; panel ID and PC password [911] as for a cable connection. Connect.</li>
<li>When finished, disconnect in BabyWare and press <b>Exit BabyWare mode</b> (or Reboot). Normal operation returns only after that reboot - or automatically when the chosen waiting time passes without BabyWare connected.</li>
</ol>
<div class="warn"><b>BabyWare mode stops everything except the web page</b>: the bridge's panel connection, MQTT / Home Assistant (shown unavailable), HomeKit, Homey updates, network firmware upload and the display pages (the display shows <i>BabyWare mode</i>). BabyWare may disconnect and reconnect. <b>Only a reboot restores normal operation.</b></div>
<div class="danger"><b>BabyWare can change the panel's programming.</b> The bridge forwards whatever BabyWare sends. The pass-through port has no password of its own: it is off by default, always off after a reboot, and the bridge reboots to normal operation when the chosen time passes without BabyWare connected (a running session is never cut; an idle one closes after 10 minutes). BabyWare still needs the panel's PC password / installer credentials.</div>
<p class="mute">At 9600 baud reading the whole panel takes a few minutes - the same as with a cable.</p>
</section>

<section id="updates"><h2>12. Firmware updates and backups</h2>
<ul>
<li><b>Update:</b> <i>System &rarr; Firmware update</i> &rarr; choose <code>firmware.bin</code>. Settings are kept.</li>
<li>Settings are stored in the bridge's flash and survive updates; a firmware with different factory defaults never changes them.</li>
<li>Some updates change the flash layout (e.g. 1.14.0 for HomeKit) and must be installed <b>over USB once</b>; the release notes say so.</li>
<li><b>Backup / restore</b> (<i>System &rarr; Backup</i>): all settings plus zone names, modes and sensor types. Download one after every change you'd hate to redo.</li>
</ul></section>

<section id="recovery"><h2>13. Forgot the password / can't reach the bridge</h2>
<h3>Reset the settings</h3>
<ol>
<li>Unplug and re-plug the power (or press RST / EN), then <b>within 5 seconds press the BOOT button (left button on the T-Display) and hold it for 10 seconds</b>. The display counts down; releasing early cancels. Don't hold it <i>while</i> plugging in - the ESP32 would start in flashing mode (just re-plug). Or type <code>config reset</code> on the USB console.</li>
<li>The bridge restarts with the factory defaults: login <b>admin / changeme</b>, WiFi from the firmware defaults (otherwise the <b>ParadoxBridge</b> access point at <code>192.168.4.1</code>).</li>
<li>Log in, then <b>restore your backup</b> (System &rarr; Backup) - or enter the settings again.</li>
<li>Update the password in the Homey app settings.</li>
</ol>
<p><b>Reset:</b> web login, WiFi / IP, panel login (back to PC password), MQTT / HomeKit, all other settings. <b>Kept:</b> zone names / modes / sensor types and the cached panel labels.</p>
<h3>Can't find the bridge on the network</h3>
<ul>
<li>Look at the display - it shows the current IP address (or the access point).</li>
<li>Try <code>http://paradox-<code>http://paradox.local</code>lt;last IP number<code>http://paradox.local</code>gt;.local</code> (e.g. <code>paradox-84.local</code>), or your router's list of devices.</li>
<li>If it can't join your WiFi for 30 seconds it opens the <b>ParadoxBridge</b> access point; connect and open <code>http://192.168.4.1</code>.</li>
<li>After a wrong static IP: reset the settings (above) or join the access point.</li>
</ul></section>

<section id="trouble"><h2>14. Troubleshooting</h2>
<div class="tw"><table>
<tr><th>Symptom</th><th>What to do</th></tr>
<tr><td>"No response from panel"</td><td>Check wiring (TX&harr;RX crossed, GND, divider) and the RX/TX pins in Settings &rarr; System. Is another program (BabyWare) connected?</td></tr>
<tr><td>"Panel refused login: invalid PC password / invalid user code"</td><td>Correct it on the Panel tab, Save, then <b>Connect panel</b>.</td></tr>
<tr><td>"Bad" frame counter rising</td><td>Loose wire, missing divider or interference - check the serial wiring.</td></tr>
<tr><td>Area won't arm</td><td>The area isn't ready (a zone is open) or that user code has no rights for the area.</td></tr>
<tr><td>Zones missing / too many zones</td><td>Settings &rarr; Zones: set <i>Always</i> / <i>Never</i>. HomeKit: reboot after changes.</td></tr>
<tr><td>Homey devices "unavailable"</td><td>Bridge unreachable, panel disconnected, or the password in the Homey app settings is outdated.</td></tr>
<tr><td>Home Assistant shows nothing</td><td>Smart home = MQTT, broker connected (status on the tab), discovery on; press <i>Publish Home Assistant discovery now</i>.</td></tr>
<tr><td>HomeKit "No Response"</td><td>Bridge offline or HomeKit switched off; after <i>Remove all pairings</i> add it again in the Home app.</td></tr>
<tr><td>Display dark</td><td>Short press the left button (backlight).</td></tr>
</table></div></section>

<section id="limits"><h2>15. Known limitations</h2>
<ul>
<li>Panel models not listed in the protocol document (e.g. product ID 0x1A) don't reveal zone types / areas - zones are shown or hidden by your settings only.</li>
<li>AC power and battery events are logged when the state <i>changes</i>; an existing trouble shows in the status, not as an event.</li>
<li>Voltages are the panel's raw 0-255 values, not volts.</li>
<li>Bypass toggles: sending it again un-bypasses the zone.</li>
<li>Homey can't name the Homey user who changed the alarm (see the Homey section).</li>
<li>The HomeKit accessory list updates only after a reboot.</li>
<li>Only one connection to the panel's serial port at a time.</li>
</ul></section>

<section id="console"><h2>16. USB console commands</h2>
<p class="mute">USB cable, serial terminal at 115200 baud. Type <code>help</code> for the full list.</p>
<div class="tw"><table>
<tr><td><code>status</code>, <code>zones</code>, <code>events</code></td><td>Show state</td></tr>
<tr><td><code>arm 1 full|stay|sleep</code>, <code>disarm 1</code></td><td>Arm / disarm area 1</td></tr>
<tr><td><code>bypass 5</code>, <code>pgm 2 on</code>, <code>beep</code></td><td>Panel actions</td></tr>
<tr><td><code>panel connect|disconnect</code></td><td>Session control</td></tr>
<tr><td><code>config</code> / <code>config reset</code></td><td>Show settings / reset them to factory defaults</td></tr>
<tr><td><code>raw on|off</code></td><td>Show every frame exchanged with the panel (diagnostics)</td></tr>
<tr><td><code>reboot</code></td><td>Restart</td></tr>
</table></div></section>

<section id="ref"><h2>17. Addresses and ports</h2>
<div class="tw"><table>
<tr><td>Web page / API</td><td><code>http://&lt;bridge IP&gt;</code> or <code>http://paradox-<code>http://paradox.local</code>lt;last IP number<code>http://paradox.local</code>gt;.local</code> (port 80)</td></tr>
<tr><td>This manual</td><td><code>/manual</code> - no login needed</td></tr>
<tr><td>Access point</td><td><code>ParadoxBridge</code>, <code>http://192.168.4.1</code></td></tr>
<tr><td>HomeKit</td><td>port 1201</td></tr>
<tr><td>Network firmware upload (PlatformIO)</td><td>port 3232, password = web password</td></tr>
<tr><td>BabyWare pass-through</td><td>TCP port 10000 (setting), RFC 2217 or raw, only while switched on</td></tr>
<tr><td>Discovery (Homey)</td><td>mDNS <code>_paradox._tcp</code></td></tr>
<tr><td>Panel serial</td><td>9600 baud, 8N1, 5 V TTL</td></tr>
</table></div>
<p class="mute">Firmware v)HTML" FW_VERSION R"HTML(.</p></section>

</main></body></html>)HTML";
