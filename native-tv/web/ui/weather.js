// The weather on Home (Display > Weather): the place is found with
// Open-Meteo's search on the TV, the degrees and the switch are saved at once,
// and the next five days are shown as the TV reads them.
function wxDayName(date) {
  const day = new Date(date + 'T12:00:00');
  return isNaN(day) ? date : day.toLocaleDateString(document.documentElement.lang || undefined, { weekday: 'short', day: 'numeric' });
}

function wxFill(d) {
  q('wx-enabled').checked = d.enabled !== false;
  q('wx-units').value = d.units === 'f' ? 'f' : 'c';
  const place = (d.place && d.place.name) || '';
  q('wx-status').textContent = d.state === 'off' ? t('wx.off', 'Off')
    : d.now ? t('wx.now', '{place}: {temp}° now', { place: place, temp: String(d.now.temp) })
      : d.state === 'offline' ? t('wx.offline', 'Open-Meteo is not answering. The TV tries again in a few minutes.')
        : t('wx.loading', 'Reading the forecast for {place}…', { place: place });
  const days = q('wx-days');
  days.textContent = '';
  (d.days || []).forEach(function (day) {
    const cell = document.createElement('div');
    cell.className = 'sub';
    cell.textContent = wxDayName(day.date) + ' · ' + day.high + '° / ' + day.low + '° · ' + day.rain + '%';
    days.appendChild(cell);
  });
}

async function loadWeather() {
  try { wxFill(await (await fetch(api('/api/home/weather'))).json()); }
  catch (e) { q('wx-status').textContent = e.message; }
}

async function wxSave(change) {
  try {
    const res = await fetch(api('/api/home/weather'), {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(change)
    });
    const d = await res.json();
    if (!d || d.ok === false) throw new Error((d && d.error) || t('wx.failed', 'That did not work'));
    wxFill(d);
    if (change.place) setTimeout(loadWeather, 2500);
  } catch (e) { q('wx-status').textContent = e.message; }
}

async function wxSearch() {
  const list = q('wx-places');
  list.textContent = '';
  try {
    const d = await (await fetch(api('/api/home/weather/search?q=' + encodeURIComponent(q('wx-query').value)))).json();
    const places = (d && d.places) || [];
    if (!places.length) { list.textContent = t('wx.none', 'No place with that name'); return; }
    places.forEach(function (place) {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'btn btn-tonal';
      button.style.margin = '.25em';
      button.textContent = place.name;
      button.onclick = function () { list.textContent = ''; wxSave({ place: place }); };
      list.appendChild(button);
    });
  } catch (e) { list.textContent = e.message; }
}
