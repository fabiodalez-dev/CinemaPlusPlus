// Cinema++ updates (Settings > Server): the TV asks GitHub for the latest
// release, and on Install downloads it, checks its SHA-256 and installs it.
let updateTimer = 0;

function updateFill(d) {
  const button = q('btn-update-install');
  button.hidden = !d.available || d.state === 'downloading' || d.state === 'installing';
  let text = t('update.current', 'Installed: {v}', { v: d.current || '?' });
  if (d.state === 'checking') text += ' · ' + t('update.checking', 'asking GitHub…');
  else if (d.state === 'downloading') text += ' · ' + t('update.downloading', 'downloading {v}…', { v: d.latest });
  else if (d.state === 'installing') text += ' · ' + t('update.installing', 'installing {v}, the service restarts', { v: d.latest });
  else if (d.state === 'failed') text += ' · ' + (d.error || t('update.failed', 'the update did not work'));
  else if (d.available) text += ' · ' + t('update.available', '{v} is available', { v: d.latest });
  else if (d.latest) text += ' · ' + t('update.latest', 'this is the latest');
  q('update-status').textContent = text;
  clearTimeout(updateTimer);
  if (d.state === 'checking' || d.state === 'downloading' || d.state === 'installing')
    updateTimer = setTimeout(function () { loadCinemaUpdate(false); }, 2000);
}

async function loadCinemaUpdate(check) {
  try { updateFill(await (await fetch(api('/api/cinemapp/update' + (check === false ? '' : '?check=1')))).json()); }
  catch (e) { q('update-status').textContent = e.message; }
}

async function updateAct(action) {
  try {
    const res = await fetch(api('/api/cinemapp/update'), {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ action: action })
    });
    const d = await res.json();
    if (!d || d.ok === false) throw new Error((d && d.error) || t('update.failed', 'the update did not work'));
    updateFill(d);
  } catch (e) { q('update-status').textContent = e.message; }
}

// Bloomee and Cinema web from their GitHub releases (Settings > Server): Update hands the IPK's
// URL and SHA-256 to the install from a URL, under Apps, which shows its progress.
let appsUpdateTimer = 0;

async function loadAppsUpdate(check) {
  const list = q('appsupdate-list');
  try {
    const d = await (await fetch(api('/api/cinemapp/apps' + (check ? '?check=1' : '')))).json();
    if (check || d.state === 'checking') {
      clearTimeout(appsUpdateTimer);
      const again = await (await fetch(api('/api/cinemapp/apps'))).json();
      if (again.state === 'checking') {
        list.textContent = t('update.checking', 'asking GitHub…');
        appsUpdateTimer = setTimeout(function () { loadAppsUpdate(false); }, 2000);
        return;
      }
      return appsUpdateFill(again);
    }
    appsUpdateFill(d);
  } catch (e) { list.textContent = e.message; }
}

function appsUpdateFill(d) {
  const list = q('appsupdate-list');
  list.textContent = '';
  if (!d.apps || !d.apps.length) {
    list.textContent = t('appsupdate.none', 'Press Check now to ask GitHub.');
    return;
  }
  d.apps.forEach(function (a) {
    const li = document.createElement('li');
    let text = a.name + ' ' + (a.installed || '?');
    if (a.error) text += ' · ' + a.error;
    else if (a.available) text += ' · ' + t('update.available', '{v} is available', { v: a.latest });
    else if (a.latest) text += ' · ' + t('update.latest', 'this is the latest');
    li.textContent = text + ' ';
    if (a.available && a.ipkUrl && a.sha256) {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn btn-filled';
      b.textContent = t('appsupdate.install', 'Update');
      b.onclick = function () { appsUpdateInstall(a); };
      li.appendChild(b);
    }
    list.appendChild(li);
  });
}

function appsUpdateInstall(a) {
  showTab('catalogue');
  const box = q('disc-sideload-apps');
  if (box) box.open = true;
  q('sl-url').value = a.ipkUrl;
  q('sl-sha').value = a.sha256;
  startUrlInstall();
}

// Cinema++ as the first app at power-on (Settings > Server).
async function loadFirstApp() {
  try {
    const d = await (await fetch(api('/api/cinemapp/first-app'))).json();
    q('firstapp-enabled').checked = !!d.enabled;
    q('firstapp-enabled').disabled = d.ok === false;
    q('firstapp-status').textContent = d.ok === false ? (d.error || '') : '';
  } catch (e) { q('firstapp-status').textContent = e.message; }
}

async function firstAppSave(enabled) {
  const box = q('firstapp-enabled');
  box.disabled = true;
  q('firstapp-status').textContent = t('firstapp.working', 'Working…');
  try {
    const res = await fetch(api('/api/cinemapp/first-app'), {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ enabled: enabled })
    });
    const d = await res.json();
    if (!d || d.ok === false) throw new Error((d && d.error) || t('firstapp.failed', 'Could not change it'));
    box.checked = !!d.enabled;
    q('firstapp-status').textContent = d.enabled
      ? t('firstapp.on', 'On: Cinema++ opens at the next power-on.')
      : t('firstapp.off', 'Off: the TV starts as before.');
  } catch (e) {
    box.checked = !enabled;
    q('firstapp-status').textContent = e.message;
  }
  box.disabled = false;
}

// Diagnostics (Settings > Server): the TV's own summary of Cinema++.
async function loadDiagnostics() {
  try {
    const d = await (await fetch(api('/api/cinemapp/diagnostics'))).json();
    const lines = [
      'Cinema++ ' + d.cinemapp + ' · Native Tv ' + d.nativetv,
      t('diag.homeKey', 'Home key') + ': ' + d.homeKey,
      t('diag.bootHook', 'Service at boot') + ': ' + (d.bootHook ? '✓' : '✗') + ' · ' +
        t('diag.firstApp', 'first app') + ': ' + (d.firstApp ? '✓' : '✗'),
      'Jellyfin: ' + d.jellyfin + ' · ' + t('diag.weather', 'weather') + ': ' + d.weather + ' · ' +
        t('diag.music', 'music') + ': ' + d.music,
      'Live: ' + (d.live && d.live.playing ? (d.live.source || 'tv') : '-'),
      t('diag.memory', 'Free memory') + ': ' + Math.round((d.memAvailableKb || 0) / 1024) + ' MB'
    ];
    (d.processes || []).forEach(function (p) { lines.push(p.name + ' (' + p.pid + '): ' + Math.round(p.rssKb / 1024) + ' MB'); });
    q('diag-out').textContent = lines.join('\n');
  } catch (e) { q('diag-out').textContent = e.message; }
}

function copyDiagnostics() {
  const text = q('diag-out').textContent;
  try { navigator.clipboard.writeText(text); } catch (e) { /* the text stays there to select */ }
}
