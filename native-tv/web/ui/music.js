// Music on Home (Display > Music): Cinema's built-in tracks or a USB stick's
// lounge/music folder, played by the TV while Home shows. Every change is
// saved at once; the TV starts again from it.
function musicFill(d) {
  q('music-enabled').checked = !!d.enabled;
  q('music-source').value = d.source === 'usb' ? 'usb' : 'builtin';
  q('music-volume').value = d.volume || 35;
  q('music-shuffle').checked = d.shuffle !== false;
  const list = q('music-tracks');
  list.textContent = '';
  if (d.source !== 'usb') {
    const chosen = d.tracks || [];
    (d.builtin || []).forEach(function (track) {
      const row = document.createElement('label');
      row.className = 'setting-row';
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.checked = !chosen.length || chosen.indexOf(track.id) >= 0;
      box.onchange = function () {
        const ids = [].slice.call(list.querySelectorAll('input')).map(function (input, i) {
          return input.checked ? d.builtin[i].id : null;
        }).filter(Boolean);
        musicSave({ tracks: ids.length === d.builtin.length ? [] : ids });
      };
      row.appendChild(box);
      row.appendChild(document.createTextNode(' ' + track.title + ' · ' + track.artist));
      list.appendChild(row);
    });
  }
  q('music-status').textContent = d.source === 'usb' && !d.usbTracks
    ? t('music.noUsb', 'No tracks found in lounge/music on a USB stick.')
    : d.track ? t('music.playing', 'Playing: {title}', { title: d.track.title })
      : d.paused ? t('music.paused', 'Paused') : '';
}

async function loadMusic() {
  try { musicFill(await (await fetch(api('/api/music'))).json()); }
  catch (e) { q('music-status').textContent = e.message; }
}

async function musicSave(change) {
  try {
    const res = await fetch(api('/api/music'), {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(change)
    });
    const d = await res.json();
    if (!d || d.ok === false) throw new Error((d && d.error) || t('music.failed', 'That did not work'));
    musicFill(d);
    setTimeout(loadMusic, 3000);
  } catch (e) { q('music-status').textContent = e.message; }
}
