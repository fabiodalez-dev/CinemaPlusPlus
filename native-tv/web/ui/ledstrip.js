// The LED strip behind the TV (Display > LED strip): Cerca finds strips in
// range, Prova shows a colour or the room light, Salva keeps the settings.
// The strip's own process (nativetv-ambilight) follows the screen while the
// TV is on and gives the room light when it goes off.
// Names are looked up when the tab is first shown, once i18n.js has its words.
function ledAmbientNames() {
  return [
    ['warm', t('led.ambient.warm', 'Warm white')], ['white', t('led.ambient.white', 'White')],
    ['orange', t('led.ambient.orange', 'Orange')], ['red', t('led.ambient.red', 'Red')],
    ['pink', t('led.ambient.pink', 'Pink')], ['purple', t('led.ambient.purple', 'Purple')],
    ['blue', t('led.ambient.blue', 'Blue')], ['sky', t('led.ambient.sky', 'Light blue')],
    ['green', t('led.ambient.green', 'Green')], ['off', t('led.ambient.off', 'Off')],
    ['keep', t('led.ambient.keep', 'The last colour of the film')]
  ];
}
const LED_RGB = {
  warm: [255, 120, 30], white: [255, 255, 255], orange: [255, 70, 0], red: [255, 0, 0], pink: [255, 30, 110],
  purple: [140, 0, 255], blue: [0, 40, 255], sky: [0, 170, 255], green: [0, 255, 40], off: [0, 0, 0]
};
let ledState = null;

async function ledPost(path, body) {
  const res = await fetch(api(path), {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {})
  });
  const d = await res.json();
  if (!d || d.ok === false) throw new Error((d && d.error) || 'unknown error');
  return d;
}

function ledFill(d) {
  ledState = d;
  const c = d.conf || {};
  const pick = q('led-address');
  if (pick && c.address && ![...pick.options].some(o => o.value === c.address)) {
    pick.add(new Option((c.name || 'Strip') + ' · ' + c.address, c.address));
  }
  if (pick) pick.value = c.address || '';
  q('led-follow').checked = !!c.follow;
  q('led-brightness').value = c.brightness || 100;
  q('led-ambient').value = c.ambient || 'warm';
  q('led-ambient-brightness').value = c.ambientBrightness || 60;
  q('led-status').textContent = d.running
    ? t('led.running', 'Following the screen')
    : (c.follow ? t('led.starting', 'Starting…') : t('led.stopped', 'Not following the screen'));
  q('led-log').textContent = (d.log || []).join('\n');
}

async function loadLedStrip() {
  const select = q('led-ambient');
  if (select && !select.options.length) {
    ledAmbientNames().forEach(([id, name]) => select.add(new Option(name, id)));
  }
  try {
    const res = await fetch(api('/api/ledstrip'), { cache: 'no-store' });
    ledFill(await res.json());
  } catch (e) {
    q('led-status').textContent = e.message;
  }
}

async function ledScan() {
  const btn = q('btn-led-scan');
  markBusy(btn);
  q('led-scan-msg').textContent = t('led.scanning', 'Looking for strips for ten seconds…');
  try {
    const d = await ledPost('/api/ledstrip/scan');
    const pick = q('led-address');
    const keep = pick.value;
    [...pick.options].slice(1).forEach(o => o.remove());
    d.devices.forEach(dev => pick.add(new Option((dev.name || 'Strip') + ' · ' + dev.address +
      ' · ' + dev.rssi + ' dBm', dev.address)));
    if (keep && ![...pick.options].some(o => o.value === keep)) {
      pick.add(new Option(t('led.saved', 'Saved strip') + ' · ' + keep, keep));
    }
    pick.value = keep || (d.devices[0] && d.devices[0].address) || '';
    q('led-scan-msg').textContent = d.devices.length
      ? t('led.found', '{n} found', { n: d.devices.length })
      : t('led.none', 'None found. Close the Lotus Lantern app on the phone: a strip takes one connection at a time.');
  } catch (e) {
    q('led-scan-msg').textContent = e.message;
  } finally {
    if (btn) btn.classList.remove('busy');
  }
}

async function ledTry() {
  const id = q('led-ambient').value;
  const pct = Number(q('led-ambient-brightness').value) || 60;
  const rgb = (LED_RGB[id] || [255, 120, 30]).map(v => Math.round(v * pct / 100));
  try {
    await ledPost('/api/ledstrip/preview', { address: q('led-address').value, rgb });
    q('led-scan-msg').textContent = t('led.tried', 'Shown on the strip. Prova does not save: Save keeps it for when the TV is off.');
  } catch (e) {
    q('led-scan-msg').textContent = e.message;
  }
}

async function ledSave() {
  const btn = q('btn-led-save');
  markBusy(btn);
  const pick = q('led-address');
  const option = pick.options[pick.selectedIndex];
  try {
    const d = await ledPost('/api/ledstrip', {
      address: pick.value || null,
      name: option && pick.value ? option.text.split(' · ')[0] : '',
      follow: q('led-follow').checked,
      brightness: Number(q('led-brightness').value),
      ambient: q('led-ambient').value,
      ambientBrightness: Number(q('led-ambient-brightness').value)
    });
    ledFill(d);
    q('led-scan-msg').textContent = t('common.saved', 'Saved');
  } catch (e) {
    q('led-scan-msg').textContent = e.message;
  } finally {
    if (btn) btn.classList.remove('busy');
  }
}
