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
