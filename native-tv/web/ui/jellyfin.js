// The Jellyfin library (Apps > Jellyfin): Quick Connect links a server without
// a password, Home then shows its rows and search finds its titles. While a
// code waits, the tab asks the TV every two seconds; the TV asks the server.
let jfTimer = 0;

async function jfGet() {
  const res = await fetch(api('/api/jellyfin/link'));
  return res.json();
}

async function jfPost(body) {
  const res = await fetch(api('/api/jellyfin/link'), {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
  });
  const d = await res.json().catch(() => null);
  if (!d || d.ok === false) throw new Error((d && d.error) || t('jf.failed', 'That did not work'));
  return d;
}

function jfFill(d) {
  const waiting = d.state === 'waiting';
  const linked = d.state === 'linked';
  q('jf-code-box').hidden = !waiting;
  q('jf-server-row').hidden = waiting || linked;
  q('jf-linked-row').hidden = !linked;
  q('jf-code').textContent = waiting ? d.code : '';
  q('jf-status').textContent = linked
    ? t('jf.linked', 'Linked to {server} as {user}', { server: d.server, user: d.user || '' })
    : waiting ? t('jf.waiting', 'Waiting for the code to be approved ({n} s left)', { n: String(d.expiresInS || 0) })
      : t('jf.unlinked', 'Not linked');
  if (d.error) q('jf-msg').textContent = d.error;
}

async function jfPoll() {
  try { jfFill(await jfGet()); } catch (e) { q('jf-msg').textContent = e.message; }
}

function jfPolling(on) {
  clearInterval(jfTimer);
  jfTimer = 0;
  if (!on) return;
  jfPoll();
  jfTimer = setInterval(jfPoll, 2000);
}

async function jfStart() {
  q('jf-msg').textContent = '';
  const button = q('btn-jf-start');
  button.disabled = true;
  try { jfFill(await jfPost({ action: 'start', server: q('jf-server').value })); }
  catch (e) { q('jf-msg').textContent = e.message; }
  finally { button.disabled = false; }
}

async function jfAction(action) {
  q('jf-msg').textContent = '';
  try { jfFill(await jfPost({ action: action })); } catch (e) { q('jf-msg').textContent = e.message; }
}

async function jfRefresh() {
  try {
    await fetch(api('/api/home/jellyfin/refresh'), {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}'
    });
    q('jf-msg').textContent = t('jf.refreshing', 'Home is reading the library again.');
  } catch (e) { q('jf-msg').textContent = e.message; }
}
