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

// Diagnostics (Settings > Server): the TV's own summary of Cinema++.
async function loadDiagnostics() {
  try {
    const d = await (await fetch(api('/api/cinemapp/diagnostics'))).json();
    const lines = [
      'Cinema++ ' + d.cinemapp + ' · Native Tv ' + d.nativetv,
      t('diag.homeKey', 'Home key') + ': ' + d.homeKey,
      t('diag.bootHook', 'Service at boot') + ': ' + (d.bootHook ? '✓' : '✗'),
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
