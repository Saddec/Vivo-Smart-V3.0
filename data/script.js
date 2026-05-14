// ========== تسجيل الدخول ==========
function doLogin() {
  const password = document.getElementById('loginPassword').value;
  fetch('/api/login', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ password: password })
  })
  .then(r => r.json())
  .then(data => {
    if (data.success) {
      sessionStorage.setItem('auth', 'true');
      document.getElementById('loginOverlay').style.display = 'none';
      document.getElementById('mainContent').style.display = 'block';
      showTab('dashboard');
    } else {
      document.getElementById('loginError').style.display = 'block';
    }
  });
}

// ========== التنقل بين التبويبات ==========
function showTab(tabName) {
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  const el = document.getElementById('tab-' + tabName);
  if (el) el.classList.add('active');
  document.querySelectorAll('.sidebar a').forEach(a => a.classList.remove('active'));
  const link = document.querySelector('a[href="#' + tabName + '"]');
  if (link) link.classList.add('active');
  if (window.innerWidth < 768) document.querySelector('.sidebar').classList.remove('open');
}
window.addEventListener('hashchange', () => showTab(location.hash.substring(1) || 'dashboard'));

// ========== الوقت والتاريخ ==========
function updateClock() {
  fetch('/api/time').then(r => r.text()).then(t => {
    const el = document.getElementById('timeDisplay');
    if (el) el.innerText = t;
  });
  fetch('/api/date').then(r => r.json()).then(d => {
    const gel = document.getElementById('gregDate');
    const hel = document.getElementById('hijriDate');
    if (gel) gel.innerText = d.greg;
    if (hel) hel.innerText = d.hijri;
  });
}
setInterval(updateClock, 1000);
updateClock();

function fetchStatus() {
  fetch('/api/status').then(r => r.json()).then(s => {
    const pel = document.getElementById('playingStatus');
    if (pel) pel.innerText = s.playing ? 'يتم التشغيل: ' + s.file : 'متوقف';
    const vel = document.getElementById('volumeSlider');
    if (vel) vel.value = s.volume;
  });
}
setInterval(fetchStatus, 2000);

// ========== التحكم السريع بالصوت ==========
function setVolume(v) { fetch('/api/volume?level=' + v); }
function stopAudio() { fetch('/api/stop'); }
function triggerAdhan(p) { fetch('/api/adhan?prayer=' + p); }
function triggerIqama() { fetch('/api/iqama'); }

// ========== WiFi ==========
function scanWiFi() {
  fetch('/api/wifi/scan').then(r => r.json()).then(nets => {
    let html = '';
    nets.forEach(n => html += '<div onclick="document.getElementById(\'ssid\').value=\'' + n.ssid + '\'">' + n.ssid + ' (' + n.rssi + 'dBm)</div>');
    const wl = document.getElementById('wifiList');
    if (wl) wl.innerHTML = html;
  });
}
function toggleDHCP() {
  const sf = document.getElementById('staticIPFields');
  if (sf) sf.style.display = document.getElementById('dhcpToggle').checked ? 'none' : 'block';
}
function saveNetwork() {
  let data = {
    ssid: document.getElementById('ssid').value,
    pass: document.getElementById('wifiPass').value,
    dhcp: document.getElementById('dhcpToggle').checked,
    ip: document.getElementById('staticIP').value,
    gw: document.getElementById('gateway').value,
    mask: document.getElementById('subnet').value,
    dns: document.getElementById('dns').value
  };
  fetch('/api/wifi/save', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) });
  alert('تم الحفظ');
}

// ========== الدول والمدن ==========
let allCountries = [];
function loadCountries() {
  fetch('/api/location/countries').then(r => r.json()).then(data => {
    allCountries = data;
    let sel = document.getElementById('countrySelect');
    if (!sel) return;
    sel.innerHTML = '<option value="">اختر الدولة</option>';
    allCountries.forEach(c => { let o = document.createElement('option'); o.value = c; o.textContent = c; sel.appendChild(o); });
  });
}
function onCountryChange() {
  let country = document.getElementById('countrySelect').value;
  let citySel = document.getElementById('citySelect');
  if (!citySel) return;
  citySel.innerHTML = '<option value="">اختر المدينة</option>';
  if (!country) return;
  fetch('/api/location/cities?country=' + encodeURIComponent(country)).then(r => r.json()).then(cities => {
    cities.forEach(city => { let o = document.createElement('option'); o.value = city; o.textContent = city; citySel.appendChild(o); });
  });
}
function fetchPrayerTimes() {
  let country = document.getElementById('countrySelect').value;
  let city = document.getElementById('citySelect').value;
  let method = document.getElementById('methodSelect').value;
  if (!country || !city) return alert('اختر الدولة والمدينة');
  fetch('/api/prayer/fetch?country=' + encodeURIComponent(country) + '&city=' + encodeURIComponent(city) + '&method=' + method)
    .then(r => r.json())
    .then(times => {
      const np = document.getElementById('nextPrayer');
      if (np) np.innerText = 'الفجر ' + times.fajr + ' | الظهر ' + times.dhuhr + ' | العصر ' + times.asr + ' | المغرب ' + times.maghrib + ' | العشاء ' + times.isha;
      updatePrayerDisplay(times);
    });
}
function updatePrayerDisplay(times) {
  if (!times) return;
  const ids = ['fajr', 'dhuhr', 'asr', 'maghrib', 'isha'];
  ids.forEach(id => {
    const el = document.getElementById(id + 'Time');
    if (el) el.innerText = times[id] || '--:--';
  });
}
function saveOffsets() {
  alert('تم حفظ الإزاحات');
}

// ========== ملفات ==========
let currentDir = '/';
function loadFileList(dir = currentDir) {
  currentDir = dir;
  fetch('/api/files/list?dir=' + encodeURIComponent(dir))
    .then(r => r.json())
    .then(files => {
      let html = '';
      if (dir !== '/') {
        html += '<div class="file-item" onclick="loadFileList(\'/\')" style="cursor:pointer;"><i class="fas fa-arrow-up"></i> ... (العودة للجذر)</div>';
      }
      files.forEach(f => {
        let icon = f.isDirectory ? '<i class="fas fa-folder"></i>' : '<i class="fas fa-file-audio"></i>';
        let sizeStr = f.isDirectory ? '' : (f.size / 1024 / 1024).toFixed(2) + ' MB';
        html += '<div class="file-item"><span>' + icon + ' ' + f.name + ' ' + sizeStr + '</span><span>' +
          (!f.isDirectory ? '<i class="fas fa-headphones" onclick="previewFile(\'' + dir + f.name + '\')" style="color:#fff;cursor:pointer;margin:0 8px" title="معاينة"></i>' : '') +
          '<button class="btn" onclick="' + (f.isDirectory ? 'loadFileList(\'' + dir + f.name + '/\')' : 'playFile(\'' + dir + f.name + '\')') + '"><i class="fas fa-' + (f.isDirectory ? 'folder-open' : 'play') + '"></i> ' + (f.isDirectory ? 'فتح' : 'تشغيل') + '</button>' +
          '<button class="btn btn-danger" onclick="deleteFile(\'' + dir + f.name + '\')"><i class="fas fa-trash"></i></button>' +
          '<button class="btn" onclick="renameFile(\'' + dir + f.name + '\')"><i class="fas fa-edit"></i></button></span></div>';
      });
      const fl = document.getElementById('fileList');
      if (fl) fl.innerHTML = html;
      let allFiles = files.filter(f => !f.isDirectory).map(f => f.name);
      populateSelects(allFiles);
    });
}
function uploadFile() {
  let f = document.getElementById('fileInput').files[0];
  if (!f) return;
  let form = new FormData();
  form.append('file', f);
  let xhr = new XMLHttpRequest();
  xhr.open('POST', '/upload');
  xhr.onload = function () { loadFileList(currentDir); };
  xhr.send(form);
}
function previewFile(name) {
  const pc = document.getElementById('previewCard');
  if (pc) pc.style.display = 'block';
  const pn = document.getElementById('previewName');
  if (pn) pn.innerText = name;
  const ap = document.getElementById('audioPlayer');
  if (ap) { ap.src = '/api/files/stream?file=' + encodeURIComponent(name); ap.play(); }
}
function closePreview() {
  const pc = document.getElementById('previewCard');
  if (pc) pc.style.display = 'none';
  const ap = document.getElementById('audioPlayer');
  if (ap) ap.pause();
}
function deleteFile(path) {
  if (!confirm('حذف ' + path + '؟')) return;
  fetch('/api/files/delete?file=' + encodeURIComponent(path), { method: 'DELETE' })
    .then(() => loadFileList(currentDir));
}
function renameFile(oldPath) {
  let newName = prompt('الاسم الجديد:', oldPath.split('/').pop());
  if (newName) {
    let newPath = oldPath.substring(0, oldPath.lastIndexOf('/') + 1) + newName;
    fetch('/api/files/rename?old=' + encodeURIComponent(oldPath) + '&new=' + encodeURIComponent(newPath), { method: 'POST' })
      .then(() => loadFileList(currentDir));
  }
}
function createFolder() {
  let name = document.getElementById('newFolderName').value;
  if (!name) return;
  fetch('/api/files/mkdir?name=' + encodeURIComponent(name), { method: 'POST' })
    .then(() => { document.getElementById('newFolderName').value = ''; loadFileList(currentDir); });
}
function populateSelects(files) {
  ['scheduleFile', 'musicFile', 'inputFile', 'alertForGPIO', 'startupFileSelect', 'fajrAdhanFileSelect', 'adhanFileSelect', 'iqamaFileSelect', 'playlistFileSelect', 'eidTakbeerFile'].forEach(id => {
    let sel = document.getElementById(id);
    if (!sel) return;
    sel.innerHTML = '<option value="">اختر</option>';
    files.forEach(f => { let o = document.createElement('option'); o.value = f; o.textContent = f; sel.appendChild(o); });
  });
}
function playFile(name) { fetch('/api/player/play?file=' + encodeURIComponent(name) + '&duration=0'); }

// ========== الجدولة ==========
function toggleScheduleFields() {
  let type = document.getElementById('scheduleType').value;
  let div = document.getElementById('scheduleExtraFields');
  if (!div) return;
  if (type === 'weekly') div.innerHTML = '<label>يوم الأسبوع</label><select id="weekDay"><option value="0">الأحد</option><option value="1">الإثنين</option><option value="2">الثلاثاء</option><option value="3">الأربعاء</option><option value="4">الخميس</option><option value="5">الجمعة</option><option value="6">السبت</option></select>';
  else if (type === 'monthly') div.innerHTML = '<label>يوم الشهر</label><input type="number" id="monthDay" min="1" max="31" value="1">';
  else if (type === 'specific') div.innerHTML = '<label>التاريخ</label><input type="date" id="specificDate">';
  else if (type === 'prayer_relative') div.innerHTML = `
    <label>الصلاة</label>
    <select id="prayerSelect">
      <option value="0">الفجر</option><option value="1">الظهر</option><option value="2">العصر</option>
      <option value="3">المغرب</option><option value="4">العشاء</option>
    </select>
    <label>الإزاحة</label>
    <div style="display:flex; gap:10px">
      <input type="number" id="offsetValue" value="0" style="flex:2">
      <select id="offsetUnit" style="flex:1"><option value="1">ثواني</option><option value="60">دقائق</option><option value="3600">ساعات</option></select>
    </div>
    <label>نوع الإزاحة</label>
    <select id="offsetDirection"><option value="1">بعد</option><option value="-1">قبل</option></select>
    <label>تاريخ البداية (اختياري)</label><input type="date" id="validFrom">
    <label>تاريخ النهاية (اختياري)</label><input type="date" id="validTo">
  `;
  else div.innerHTML = '';
}
function addSchedule() {
  let file = document.getElementById('scheduleFile').value;
  let type = document.getElementById('scheduleType').value;
  let time = document.getElementById('scheduleTime').value.split(':');
  let data = { file, type, hour: parseInt(time[0]), minute: parseInt(time[1]), enabled: true };
  data.volume = parseInt(document.getElementById('scheduleVolume').value);
  data.loop = document.getElementById('scheduleLoopToggle').value === 'yes' ? parseInt(document.getElementById('scheduleLoopDuration').value) * 60 : 0;
  if (type === 'weekly') data.dayOfWeek = parseInt(document.getElementById('weekDay').value);
  else if (type === 'monthly') data.dayOfMonth = parseInt(document.getElementById('monthDay').value);
  else if (type === 'specific') data.specificDate = document.getElementById('specificDate').value;
  else if (type === 'prayer_relative') {
    let offsetVal = parseInt(document.getElementById('offsetValue').value);
    let offsetUnit = parseInt(document.getElementById('offsetUnit').value);
    let direction = parseInt(document.getElementById('offsetDirection').value);
    data.isPrayerRelative = true;
    data.prayerIndex = parseInt(document.getElementById('prayerSelect').value);
    data.offsetSeconds = offsetVal * offsetUnit * direction;
    data.validFrom = document.getElementById('validFrom').value;
    data.validTo = document.getElementById('validTo').value;
  }
  fetch('/api/schedule/add', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) })
    .then(() => loadSchedules());
}
function loadSchedules() {
  fetch('/api/schedule/list').then(r => r.json()).then(arr => {
    let html = '';
    arr.forEach((a, i) => {
      let desc = a.file + ' (' + a.type + ' ' + a.hour + ':' + String(a.minute).padStart(2, '0') + ')';
      if (a.isPrayerRelative) {
        const prayerNames = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'];
        let offsetSign = a.offsetSeconds >= 0 ? 'بعد ' : 'قبل ';
        let absOffset = Math.abs(a.offsetSeconds);
        let offsetStr = absOffset >= 3600 ? (absOffset / 3600).toFixed(1) + ' ساعة' : absOffset >= 60 ? (absOffset / 60) + ' دقيقة' : absOffset + ' ثانية';
        desc = a.file + ' (مرتبط: ' + offsetSign + prayerNames[a.prayerIndex] + ' ' + offsetStr + ')';
      }
      desc += ' | الصوت: ' + (a.volume || 20) + ' | تكرار: ' + ((a.loop || 0) / 60) + ' دقيقة';
      html += '<li>' + desc + ' <button onclick="deleteSchedule(' + i + ')" class="btn btn-danger"><i class="fas fa-trash"></i></button></li>';
    });
    const sl = document.getElementById('scheduleList');
    if (sl) sl.innerHTML = html;
  });
}
function deleteSchedule(i) { fetch('/api/schedule/remove?index=' + i).then(() => loadSchedules()); }
function toggleLoopFields() {
  const lf = document.getElementById('loopFields');
  if (lf) lf.style.display = document.getElementById('scheduleLoopToggle').value === 'yes' ? 'block' : 'none';
}

// ========== GPIO ==========
function saveInputMapping() {
  let pin = document.getElementById('inputPin').value;
  let file = document.getElementById('inputFile').value;
  fetch('/api/gpio/input', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pin, file }) });
}
function saveOutputMapping() {
  let pin = document.getElementById('outputPin').value;
  let alert = document.getElementById('alertForGPIO').value;
  let duration = document.getElementById('outputDuration').value;
  fetch('/api/gpio/output', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pin, alert, duration }) });
}
function addGpioSchedule() {
  let pin = document.getElementById('gpioSchedPin').value;
  let start = document.getElementById('gpioSchedStart').value;
  let end = document.getElementById('gpioSchedEnd').value;
  fetch('/api/gpio/schedule/add', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pin, start, end }) });
}

// ========== العيد ==========
function toggleEidMode() { fetch('/api/eid/mode?enable=' + (document.getElementById('eidModeToggle').checked ? 1 : 0)); }
function triggerTakbeer() { fetch('/api/eid/takbeer'); }
function saveEidFile() { fetch('/api/eid/file', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ file: document.getElementById('eidTakbeerFile').value }) }); }

// ========== مشغل الموسيقى ==========
let playlist = [];
function addToPlaylist() { let f = document.getElementById('playlistFileSelect').value; if (f) { playlist.push(f); renderPlaylist(); } }
function renderPlaylist() {
  const pl = document.getElementById('playlist');
  if (pl) pl.innerHTML = playlist.map((f, i) => '<div class="file-item"><span>' + f + '</span><button class="btn btn-danger" onclick="playlist.splice(' + i + ',1);renderPlaylist()"><i class="fas fa-trash"></i></button></div>').join('');
}
function clearPlaylist() { playlist = []; renderPlaylist(); }
function playPlaylist() {
  fetch('/api/player/playlist', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      files: playlist,
      volume: document.getElementById('playlistVolume').value,
      respectAdhan: document.getElementById('playlistAdhanRespect').checked,
      pauseAfterAdhan: 120
    })
  });
}
function stopPlaylist() { fetch('/api/stop'); }

// ========== المغرب ==========
function saveMaghribOffset() {
  fetch('/api/maghrib/offset', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ offset: parseInt(document.getElementById('maghribOffset').value) }) });
}
const days = ["الأحد", "الإثنين", "الثلاثاء", "الأربعاء", "الخميس", "الجمعة", "السبت"];
function loadMaghribAlerts() {
  fetch('/api/maghrib/alerts').then(r => r.json()).then(arr => {
    let html = '<table style="width:100%"><tr><th>اليوم</th><th>الملف</th><th>المدة (ث)</th><th>مستوى الصوت</th><th>التكرار (دقيقة)</th><th>تفعيل</th></tr>';
    arr.forEach((a, i) => {
      html += '<tr><td>' + days[i] + '</td><td><select class="maghribFile" data-day="' + i + '"><option value="">-- لا يوجد --</option></select></td>' +
        '<td><span id="dur-' + i + '">' + (a.duration || 0) + '</span></td>' +
        '<td><input type="range" class="maghribVolume" data-day="' + i + '" min="0" max="30" value="' + (a.volume || 15) + '" oninput="document.getElementById(\'vol-' + i + '\').innerText=this.value"> <span id="vol-' + i + '">' + (a.volume || 15) + '</span></td>' +
        '<td><input type="number" class="maghribLoop" data-day="' + i + '" value="' + ((a.loop || 0) / 60) + '" min="0" style="width:60px;"> دقائق</td>' +
        '<td><label class="switch"><input type="checkbox" class="maghribEnable" data-day="' + i + '" ' + (a.enabled ? 'checked' : '') + '><span class="slider"></span></label></td></tr>';
    });
    html += '</table>';
    const ma = document.getElementById('maghribAlerts');
    if (ma) ma.innerHTML = html;
    fetch('/api/files/list').then(r => r.json()).then(files => {
      document.querySelectorAll('.maghribFile').forEach(sel => {
        sel.innerHTML = '<option value="">-- لا يوجد --</option>';
        files.forEach(f => { if (!f.isDirectory) { let o = document.createElement('option'); o.value = f.name; o.textContent = f.name; sel.appendChild(o); } });
      });
      arr.forEach((a, i) => { let sel = document.querySelector('.maghribFile[data-day="' + i + '"]'); if (sel && a.file) sel.value = a.file; });
    });
  });
}
function saveMaghribAlerts() {
  let alerts = [];
  document.querySelectorAll('.maghribFile').forEach(sel => {
    let day = sel.getAttribute('data-day');
    let file = sel.value;
    let enabled = document.querySelector('.maghribEnable[data-day="' + day + '"]').checked;
    let volume = parseInt(document.querySelector('.maghribVolume[data-day="' + day + '"]').value);
    let loopMin = parseInt(document.querySelector('.maghribLoop[data-day="' + day + '"]').value) || 0;
    alerts.push({ day: parseInt(day), file, enabled, volume, loop: loopMin * 60 });
  });
  fetch('/api/maghrib/save', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ alerts }) })
    .then(() => alert('تم الحفظ'));
}

// ========== ضبط يدوي ==========
function loadManualSettings() {
  fetch('/api/prayer/manual/status').then(r => r.json()).then(data => {
    document.getElementById('manualModeToggle').checked = data.enabled;
    document.getElementById('manFajr').value = data.times.fajr || "04:30";
    document.getElementById('manDhuhr').value = data.times.dhuhr || "12:00";
    document.getElementById('manAsr').value = data.times.asr || "15:30";
    document.getElementById('manMaghrib').value = data.times.maghrib || "18:00";
    document.getElementById('manIsha').value = data.times.isha || "19:30";
  });
}
function toggleManualMode() {
  let en = document.getElementById('manualModeToggle').checked;
  fetch('/api/prayer/manual/toggle', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ enabled: en }) });
}
function saveManualPrayerTimes() {
  let times = {
    fajr: document.getElementById('manFajr').value,
    dhuhr: document.getElementById('manDhuhr').value,
    asr: document.getElementById('manAsr').value,
    maghrib: document.getElementById('manMaghrib').value,
    isha: document.getElementById('manIsha').value
  };
  fetch('/api/prayer/manual/save', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ times: times }) })
    .then(r => r.text()).then(msg => alert(msg));
}
function setManualDateTime() {
  let date = document.getElementById('manDate').value;
  let time = document.getElementById('manTime').value;
  if (!date || !time) return;
  let datetime = date + 'T' + time + ':00';
  fetch('/api/time/set', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ datetime: datetime }) })
    .then(r => r.text()).then(msg => alert(msg));
}

// ========== OTA ==========
function startOTA() {
  let file = document.getElementById('otaFile').files[0];
  if (!file) { alert('اختر ملف .bin'); return; }
  let form = new FormData();
  form.append('update', file);
  fetch('/update', { method: 'POST', body: form })
    .then(r => r.text()).then(msg => {
      document.getElementById('otaStatus').innerText = msg;
      setTimeout(location.reload, 5000);
    });
}

// ========== CSV ==========
function uploadCSV() {
  let month = document.getElementById('csvMonthSelect').value;
  let file = document.getElementById('csvFileInput').files[0];
  if (!file) return;
  let form = new FormData();
  form.append('month', month);
  form.append('file', file);
  fetch('/api/csv/upload', { method: 'POST', body: form })
    .then(r => r.text()).then(msg => { document.getElementById('csvUploadStatus').innerHTML = '<div class="alert alert-success">' + msg + '</div>'; loadLoadedMonths(); });
}
function toggleCSVMode() {
  let en = document.getElementById('csvModeToggle').checked;
  fetch('/api/csv/mode/toggle', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ enabled: en }) });
}
function loadCSVStatus() {
  fetch('/api/csv/status').then(r => r.json()).then(data => {
    document.getElementById('csvModeToggle').checked = data.enabled;
    loadLoadedMonths();
  });
}
function loadLoadedMonths() {
  fetch('/api/csv/months').then(r => r.json()).then(months => {
    let html = '';
    const names = ["", "يناير", "فبراير", "مارس", "إبريل", "مايو", "يونيو", "يوليو", "أغسطس", "سبتمبر", "أكتوبر", "نوفمبر", "ديسمبر"];
    months.forEach(m => { html += '<span style="display:inline-block;margin:5px;padding:5px 10px;background:rgba(255,255,255,0.15);border-radius:15px;cursor:pointer;" onclick="deleteCSVMonth(' + m + ')">' + names[m] + ' <i class="fas fa-trash" style="font-size:0.7em;"></i></span>'; });
    document.getElementById('loadedMonthsList').innerHTML = html || '<span style="opacity:0.5;">لا توجد شهور محملة</span>';
  });
}
function deleteCSVMonth(month) { if (confirm('حذف شهر ' + month + '؟')) fetch('/api/csv/delete?month=' + month, { method: 'DELETE' }).then(() => loadLoadedMonths()); }

// ========== بدء التشغيل ==========
function loadStartupSettings() {
  fetch('/api/startup/status').then(r => r.json()).then(data => {
    document.getElementById('startupAlertEnabled').checked = data.enabled;
    fetch('/api/files/list?dir=/').then(r => r.json()).then(files => {
      let sel = document.getElementById('startupFileSelect');
      if (!sel) return;
      sel.innerHTML = '<option value="">اختر ملف</option>';
      files.forEach(f => { if (!f.isDirectory) { let o = document.createElement('option'); o.value = f.name; o.textContent = f.name; sel.appendChild(o); } });
      if (data.file) sel.value = data.file;
    });
  });
}
function toggleStartupAlert() {}
function saveStartupSettings() {
  let enabled = document.getElementById('startupAlertEnabled').checked;
  let file = document.getElementById('startupFileSelect').value;
  fetch('/api/startup/save', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ enabled: enabled, file: file }) })
    .then(r => r.text()).then(msg => { const ss = document.getElementById('startupSaveStatus'); if (ss) ss.innerHTML = '<div class="alert alert-success">' + msg + '</div>'; });
}

// ========== الأذان والإقامة ==========
function saveAdhanAssignments() {
  fetch('/api/adhan/assign', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      fajr: document.getElementById('fajrAdhanFileSelect').value,
      adhan: document.getElementById('adhanFileSelect').value,
      iqama: document.getElementById('iqamaFileSelect').value
    })
  }).then(r => r.text()).then(msg => alert(msg));
}

// ========== كلمة المرور ==========
function changePassword() {
  let oldPass = document.getElementById('oldPassword').value;
  let newPass = document.getElementById('newPassword').value;
  let confirm = document.getElementById('confirmPassword').value;
  if (!oldPass || !newPass || !confirm) { alert('يرجى ملء جميع الحقول'); return; }
  if (newPass !== confirm) { alert('كلمة المرور الجديدة غير متطابقة'); return; }
  fetch('/api/change_password', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ old_password: oldPass, new_password: newPass })
  })
  .then(r => r.json())
  .then(data => {
    if (data.success) alert('تم تغيير كلمة المرور بنجاح');
    else alert(data.message || 'فشل');
  });
}

// ========== إعدادات أولية ==========
window.addEventListener('load', function () {
  if (sessionStorage.getItem('auth') === 'true') {
    document.getElementById('loginOverlay').style.display = 'none';
    document.getElementById('mainContent').style.display = 'block';
    showTab('dashboard');
    if (document.getElementById('manualModeToggle')) loadManualSettings();
    if (document.getElementById('csvModeToggle')) loadCSVStatus();
    if (document.getElementById('maghribAlerts')) loadMaghribAlerts();
    if (document.getElementById('startupFileSelect')) loadStartupSettings();
    if (document.getElementById('countrySelect')) loadCountries();
  }
});