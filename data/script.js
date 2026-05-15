const appState = {
  files: [],
  countries: [],
  cities: [],
  playlist: [],
  password: localStorage.getItem('vivoPassword') || 'admin'
};

const $ = (id) => document.getElementById(id);

function toast(message) {
  alert(message);
}

function formBody(data) {
  const body = new URLSearchParams();
  Object.keys(data).forEach((key) => {
    if (data[key] !== undefined && data[key] !== null) body.append(key, data[key]);
  });
  return body;
}

function safeText(value) {
  return String(value ?? '').replace(/[&<>"']/g, (ch) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  }[ch]));
}

function safeAttr(value) {
  return safeText(value).replace(/`/g, '&#96;');
}

function apiGet(url, fallback = {}) {
  return fetch(url)
    .then((response) => {
      if (!response.ok) throw new Error(`${response.status} ${url}`);
      return response.json();
    })
    .catch((err) => {
      console.warn('GET failed:', url, err);
      return fallback;
    });
}

function apiPost(url, data = {}) {
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: formBody(data)
  }).then((response) => {
    if (!response.ok) throw new Error(`${response.status} ${url}`);
    return response.json().catch(() => ({}));
  });
}

function showTab(tabName) {
  document.querySelectorAll('.tab').forEach((tab) => tab.classList.remove('active'));
  document.querySelectorAll('.sidebar a').forEach((link) => link.classList.remove('active'));

  const tab = $(`tab-${tabName}`);
  if (tab) tab.classList.add('active');

  const nav = document.querySelector(`.sidebar a[href="#${tabName}"]`);
  if (nav) nav.classList.add('active');

  if (tabName === 'files') loadFileList();
  if (tabName === 'scheduler') loadSchedules();
  if (tabName === 'gpio') loadGpioMappings();
  if (tabName === 'gpio_schedule') loadGpioSchedules();
  if (tabName === 'maghrib') loadMaghribAlerts();
  if (tabName === 'startup') loadStartupSettings();
  if (tabName === 'csv') loadCsvStatus();
  if (tabName === 'network') loadWifiStatus();
}

function doLogin() {
  const password = $('loginPassword')?.value || '';
  apiPost('/api/password/check', { password })
    .then((data) => {
      if (data.ok) {
        $('loginOverlay').style.display = 'none';
        $('mainContent').style.display = 'block';
        $('loginError').style.display = 'none';
        initDashboard();
      } else {
        $('loginError').style.display = 'block';
      }
    })
    .catch(() => {
      if (password === appState.password) {
        $('loginOverlay').style.display = 'none';
        $('mainContent').style.display = 'block';
        initDashboard();
      } else {
        $('loginError').style.display = 'block';
      }
    });
}

function initDashboard() {
  updateClock();
  fetchStatus();
  fetchPrayerTimes();
  loadCountries();
  loadManualSettings();
  loadFileList();
  loadSchedules();
  loadMaghribAlerts();
  loadStartupSettings();
  loadCsvStatus();
  loadWifiStatus();
  populateGpioPins();
}

function updateClock() {
  fetch('/api/time')
    .then((r) => r.ok ? r.text() : '--:--')
    .then((time) => { if ($('timeDisplay')) $('timeDisplay').textContent = time; })
    .catch(() => {});

  apiGet('/api/date', {}).then((data) => {
    if ($('gregDate')) $('gregDate').textContent = data.greg || '';
    if ($('hijriDate')) $('hijriDate').textContent = data.hijri || '';
  });
}

function fetchStatus() {
  apiGet('/api/status', {}).then((data) => {
    if ($('playingStatus')) {
      $('playingStatus').textContent = data.playing ? `يعمل: ${data.file || ''}` : 'متوقف';
    }
    if ($('volumeSlider') && data.volume !== undefined) $('volumeSlider').value = data.volume;
  });
}

function setVolume(value) {
  apiPost('/api/audio/volume', { volume: value })
    .then(fetchStatus)
    .catch((err) => console.error(err));
}

function stopAudio() {
  apiPost('/api/audio/stop').then(fetchStatus).catch((err) => console.error(err));
}

function scanWiFi() {
  const btn = document.querySelector('button[onclick="scanWiFi()"]');
  if (btn) btn.disabled = true;
  if ($('wifiList')) $('wifiList').innerHTML = '<p>جاري البحث عن الشبكات...</p>';

  fetch('/api/wifi/scan')
    .then((r) => {
      if (r.status === 202) {
        setTimeout(scanWiFi, 2500);
        return null;
      }
      return r.json();
    })
    .then((data) => {
      if (!data || !data.networks) return;
      const html = data.networks.map((n) =>
        `<div class="file-item" onclick="selectNetwork('${safeAttr(n.ssid)}')"><span>${safeText(n.ssid)}</span><span>${n.rssi} dBm</span></div>`
      ).join('');
      if ($('wifiList')) $('wifiList').innerHTML = html || '<p>لا توجد شبكات</p>';
    })
    .catch((err) => {
      console.error(err);
      if ($('wifiList')) $('wifiList').innerHTML = '<p>تعذر البحث عن الشبكات</p>';
    })
    .finally(() => { if (btn) btn.disabled = false; });
}

function getNetworkForm() {
  return {
    ssid: $('ssid')?.value || '',
    pass: $('wifiPass')?.value || '',
    dhcp: $('dhcpToggle')?.checked ? '1' : '0',
    ip: $('staticIP')?.value || '192.168.1.100',
    gw: $('gateway')?.value || '192.168.1.1',
    mask: $('subnet')?.value || '255.255.255.0',
    dns: $('dns')?.value || '8.8.8.8'
  };
}

function saveNetwork() {
  apiPost('/api/wifi/save', getNetworkForm())
    .then(() => toast('تم حفظ إعدادات الشبكة'))
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function connectNetwork() {
  const data = getNetworkForm();
  if (!data.ssid) return toast('اختر أو اكتب اسم الشبكة أولاً');
  if ($('wifiStatus')) $('wifiStatus').textContent = 'جاري الاتصال بالشبكة...';

  apiPost('/api/wifi/connect', data)
    .then((result) => {
      if (result.connected) {
        const ip = result.ip || '';
        if ($('wifiStatus')) $('wifiStatus').textContent = `تم الاتصال بنجاح. IP: ${ip}`;
        toast(`تم الاتصال بالشبكة. افتح اللوحة من: http://${ip}`);
      } else {
        if ($('wifiStatus')) $('wifiStatus').textContent = 'فشل الاتصال. تحقق من كلمة المرور أو قرب الشبكة.';
        toast('فشل الاتصال. ستبقى نقطة VivoSmart-Setup متاحة.');
      }
    })
    .catch((err) => {
      if ($('wifiStatus')) $('wifiStatus').textContent = 'فشل إرسال طلب الاتصال';
      toast(`فشل الاتصال: ${err.message}`);
    });
}

function loadWifiStatus() {
  apiGet('/api/wifi/status', {}).then((data) => {
    if ($('dhcpToggle')) $('dhcpToggle').checked = !!data.dhcp;
    if ($('ssid') && data.savedSsid) $('ssid').value = data.savedSsid;
    if ($('staticIP')) $('staticIP').value = data.staticIp || '192.168.1.100';
    if ($('gateway')) $('gateway').value = data.gateway || '192.168.1.1';
    if ($('subnet')) $('subnet').value = data.subnet || '255.255.255.0';
    if ($('dns')) $('dns').value = data.dns || '8.8.8.8';
    toggleDHCP();

    if (!$('wifiStatus')) return;
    if (data.connected) {
      $('wifiStatus').textContent = `متصل: ${data.ssid || ''} - IP: ${data.ip || ''}`;
    } else {
      $('wifiStatus').textContent = 'غير متصل بشبكة خارجية. نقطة الإعداد تعمل على 192.168.4.1';
    }
  });
}

function selectNetwork(ssid) {
  if ($('ssid')) $('ssid').value = ssid;
}

function toggleDHCP() {
  if ($('staticIPFields')) $('staticIPFields').style.display = $('dhcpToggle')?.checked ? 'none' : 'block';
}

function loadCountries() {
  apiGet('/api/location/countries', { countries: [] }).then((data) => {
    appState.countries = data.countries || data || [];
    const select = $('countrySelect');
    if (!select) return;
    select.innerHTML = '<option value="">اختر الدولة</option>' +
      appState.countries.map((country) => `<option value="${safeAttr(country)}">${safeText(country)}</option>`).join('');
  });
}

function onCountryChange() {
  const country = $('countrySelect')?.value || '';
  apiGet(`/api/location/cities?country=${encodeURIComponent(country)}`, { cities: [] }).then((data) => {
    appState.cities = data.cities || data || [];
    const select = $('citySelect');
    if (!select) return;
    select.innerHTML = '<option value="">اختر المدينة</option>' +
      appState.cities.map((city) => `<option value="${safeAttr(city)}">${safeText(city)}</option>`).join('');
  });
}

function fetchPrayerTimes() {
  const country = $('countrySelect')?.value || '';
  const city = $('citySelect')?.value || '';
  const method = $('methodSelect')?.value || '0';
  const query = country && city ? `?country=${encodeURIComponent(country)}&city=${encodeURIComponent(city)}&method=${method}` : '';

  apiGet(`/api/prayer/times${query}`, {}).then((data) => {
    if ($('fajrTime')) $('fajrTime').textContent = data.fajr || '--:--';
    if ($('dhuhrTime')) $('dhuhrTime').textContent = data.dhuhr || '--:--';
    if ($('asrTime')) $('asrTime').textContent = data.asr || '--:--';
    if ($('maghribTime')) $('maghribTime').textContent = data.maghrib || '--:--';
    if ($('ishaTime')) $('ishaTime').textContent = data.isha || '--:--';
    if ($('nextPrayer')) $('nextPrayer').textContent = data.next ? `الصلاة القادمة: ${data.next}` : '';
  });
}

function saveOffsets() {
  apiPost('/api/prayer/offsets', {
    fajr: $('offsetFajr')?.value || 0,
    dhuhr: $('offsetDhuhr')?.value || 0,
    asr: $('offsetAsr')?.value || 0,
    maghrib: $('offsetMaghrib')?.value || 0,
    isha: $('offsetIsha')?.value || 0
  }).then(() => {
    toast('تم حفظ الإزاحات');
    fetchPrayerTimes();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadManualSettings() {
  apiGet('/api/prayer/manual/status', {}).then((data) => {
    if ($('manualModeToggle')) $('manualModeToggle').checked = !!data.enabled;
    ['Fajr', 'Dhuhr', 'Asr', 'Maghrib', 'Isha'].forEach((name) => {
      const key = name.toLowerCase();
      if ($(`man${name}`) && data[key]) $(`man${name}`).value = data[key];
    });
  });
}

function toggleManualMode() {
  saveManualPrayerTimes();
}

function saveManualPrayerTimes() {
  apiPost('/api/prayer/manual/save', {
    enabled: $('manualModeToggle')?.checked ? '1' : '0',
    fajr: $('manFajr')?.value || '',
    dhuhr: $('manDhuhr')?.value || '',
    asr: $('manAsr')?.value || '',
    maghrib: $('manMaghrib')?.value || '',
    isha: $('manIsha')?.value || ''
  }).then(() => toast('تم حفظ المواقيت اليدوية'))
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadFileList() {
  apiGet('/api/files/list', { files: [] }).then((data) => {
    renderSdStatus(data.sd || {});
    appState.files = data.files || [];
    const fileHtml = appState.files.map((f) => {
      const name = safeText(f.name);
      const size = f.isDirectory ? 'مجلد' : `${((f.size || 0) / 1024 / 1024).toFixed(2)} MB`;
      return `<div class="file-item"><span onclick="previewFile('${safeAttr(f.name)}')">${name}</span><span>${size}</span><button class="btn btn-danger" onclick="deleteFile('${safeAttr(f.name)}')">حذف</button></div>`;
    }).join('');
    if ($('fileList')) $('fileList').innerHTML = fileHtml || '<p>لا توجد ملفات على بطاقة SD</p>';
    populateFileSelects();
  });
}

function renderSdStatus(sd) {
  if (!$('sdStatus')) return;
  if (!sd.connected) {
    $('sdStatus').innerHTML = '<div class="file-item"><span>غير متصلة أو غير قابلة للقراءة</span><span>تحقق من التوصيل والفورمات FAT32</span></div>';
    return;
  }

  $('sdStatus').innerHTML = [
    `<div class="file-item"><span>الحالة</span><span>متصلة وتعمل</span></div>`,
    `<div class="file-item"><span>النوع</span><span>${safeText(sd.cardType || 'UNKNOWN')}</span></div>`,
    `<div class="file-item"><span>الحجم</span><span>${sd.usedMB || 0} MB مستخدم / ${sd.totalMB || 0} MB</span></div>`,
    `<div class="file-item"><span>SD GPIO</span><span>CS ${sd.cs}, SCK ${sd.sck}, MISO ${sd.miso}, MOSI ${sd.mosi}</span></div>`
  ].join('');
}

function populateFileSelects() {
  const options = appState.files
    .filter((f) => !f.isDirectory)
    .map((f) => `<option value="${safeAttr(f.name)}">${safeText(f.name)}</option>`)
    .join('');
  [
    'fajrAdhanFileSelect', 'adhanFileSelect', 'iqamaFileSelect', 'scheduleFile',
    'inputFile', 'eidTakbeerFile', 'playlistFileSelect', 'startupFileSelect'
  ].forEach((id) => { if ($(id)) $(id).innerHTML = options; });
}

function uploadFile() {
  const input = $('fileInput');
  if (!input?.files?.length) return toast('اختر ملفاً أولاً');
  const body = new FormData();
  body.append('file', input.files[0]);
  fetch('/api/files/upload', { method: 'POST', body })
    .then((r) => {
      if (!r.ok) throw new Error('upload failed');
      if ($('uploadStatus')) $('uploadStatus').textContent = 'تم الرفع';
      loadFileList();
    })
    .catch(() => {
      if ($('uploadStatus')) $('uploadStatus').textContent = 'فشل الرفع. تحقق من اتصال بطاقة SD';
    });
}

function deleteFile(name) {
  if (!confirm(`حذف ${name}؟`)) return;
  apiPost('/api/files/delete', { name }).then(loadFileList).catch((err) => toast(`فشل الحذف: ${err.message}`));
}

function createFolder() {
  const name = $('newFolderName')?.value || '';
  if (!name) return toast('اكتب اسم المجلد');
  apiPost('/api/files/mkdir', { name }).then(loadFileList).catch((err) => toast(`فشل إنشاء المجلد: ${err.message}`));
}

function previewFile(name) {
  if (!/\.(mp3|wav)$/i.test(name)) return;
  if ($('previewCard')) $('previewCard').style.display = 'block';
  if ($('previewName')) $('previewName').textContent = name;
  if ($('audioPlayer')) $('audioPlayer').src = `/sd/${encodeURIComponent(name)}`;
}

function closePreview() {
  if ($('previewCard')) $('previewCard').style.display = 'none';
  if ($('audioPlayer')) $('audioPlayer').pause();
}

function saveAdhanAssignments() {
  apiPost('/api/adhan/files', {
    fajr: $('fajrAdhanFileSelect')?.value || '',
    adhan: $('adhanFileSelect')?.value || '',
    iqama: $('iqamaFileSelect')?.value || ''
  }).then(() => toast('تم حفظ ملفات الأذان والإقامة'))
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function toggleScheduleFields() {
  const type = $('scheduleType')?.value || 'daily';
  let html = '';
  if (type === 'weekly') html = '<label>اليوم</label><select id="scheduleDay"><option value="0">الأحد</option><option value="1">الإثنين</option><option value="2">الثلاثاء</option><option value="3">الأربعاء</option><option value="4">الخميس</option><option value="5">الجمعة</option><option value="6">السبت</option></select>';
  if (type === 'monthly') html = '<label>اليوم من الشهر</label><input type="number" id="scheduleDay" min="1" max="31" value="1">';
  if (type === 'specific') html = '<label>التاريخ</label><input type="date" id="scheduleDate">';
  if (type === 'prayer_relative') html = '<label>الصلاة</label><select id="schedulePrayer"><option value="0">الفجر</option><option value="1">الظهر</option><option value="2">العصر</option><option value="3">المغرب</option><option value="4">العشاء</option></select><label>الإزاحة بالدقائق</label><input type="number" id="scheduleOffset" value="0">';
  if ($('scheduleExtraFields')) $('scheduleExtraFields').innerHTML = html;
}

function toggleLoopFields() {
  if ($('loopFields')) $('loopFields').style.display = $('scheduleLoopToggle')?.value === 'yes' ? 'block' : 'none';
}

function addSchedule() {
  const [hour = '0', minute = '0'] = ($('scheduleTime')?.value || '00:00').split(':');
  apiPost('/api/scheduler/add', {
    file: $('scheduleFile')?.value || '',
    type: $('scheduleType')?.value || 'daily',
    hour,
    minute,
    dayOfWeek: $('scheduleType')?.value === 'weekly' ? $('scheduleDay')?.value : '-1',
    dayOfMonth: $('scheduleType')?.value === 'monthly' ? $('scheduleDay')?.value : '-1',
    specificDate: $('scheduleDate')?.value || '',
    volume: $('scheduleVolume')?.value || 20,
    loop: $('scheduleLoopToggle')?.value === 'yes' ? $('scheduleLoopDuration')?.value || 0 : 0,
    prayerIndex: $('schedulePrayer')?.value || 0,
    offsetSeconds: Number($('scheduleOffset')?.value || 0) * 60
  }).then(() => {
    toast('تم حفظ التنبيه');
    loadSchedules();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadSchedules() {
  apiGet('/api/scheduler/list', []).then((data) => {
    const alerts = Array.isArray(data) ? data : (data.alerts || []);
    if (!$('scheduleList')) return;
    $('scheduleList').innerHTML = alerts.map((a, i) =>
      `<li class="file-item"><span>${safeText(a.file)} - ${safeText(a.type)} ${String(a.hour).padStart(2, '0')}:${String(a.minute).padStart(2, '0')}</span><button class="btn btn-danger" onclick="deleteSchedule(${i})">حذف</button></li>`
    ).join('');
  });
}

function deleteSchedule(index) {
  apiPost('/api/scheduler/delete', { index }).then(loadSchedules).catch((err) => toast(`فشل الحذف: ${err.message}`));
}

function populateGpioPins() {
  const pins = [3,4,5,6,7,8,9,10,11,12,13,14,15,19,47];
  if ($('gpioSchedPin')) $('gpioSchedPin').innerHTML = pins.map((p) => `<option>${p}</option>`).join('');
}

function saveInputMapping() {
  apiPost('/api/gpio/input', { pin: $('inputPin')?.value, file: $('inputFile')?.value || '' })
    .then(() => toast('تم حفظ المدخل')).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function saveOutputMapping() {
  apiPost('/api/gpio/output', { pin: $('outputPin')?.value, duration: $('outputDuration')?.value || 5 })
    .then(() => toast('تم حفظ المخرج')).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadGpioMappings() {
  apiGet('/api/gpio/mappings', {}).then(() => {});
}

function addGpioSchedule() {
  const [startHour = '0', startMin = '0'] = ($('gpioSchedStart')?.value || '00:00').split(':');
  const [endHour = '0', endMin = '0'] = ($('gpioSchedEnd')?.value || '00:00').split(':');
  apiPost('/api/gpio/schedule/add', {
    pin: $('gpioSchedPin')?.value,
    startHour,
    startMin,
    endHour,
    endMin,
    state: '1',
    type: 'daily'
  }).then(() => toast('تم حفظ الجدولة')).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadGpioSchedules() {
  apiGet('/api/gpio/schedule/list', []).then(() => {});
}

function toggleEidMode() {
  apiPost('/api/eid/mode', { enabled: $('eidModeToggle')?.checked ? '1' : '0' })
    .then(() => toast('تم تحديث وضع العيد')).catch((err) => toast(`فشل التحديث: ${err.message}`));
}

function triggerTakbeer() {
  apiPost('/api/audio/play', { file: $('eidTakbeerFile')?.value || '', priority: 1 })
    .catch((err) => toast(`فشل التشغيل: ${err.message}`));
}

function saveEidFile() {
  apiPost('/api/eid/file', { file: $('eidTakbeerFile')?.value || '' })
    .then(() => toast('تم حفظ ملف التكبيرات')).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function addToPlaylist() {
  const file = $('playlistFileSelect')?.value || '';
  if (!file) return;
  appState.playlist.push(file);
  renderPlaylist();
}

function renderPlaylist() {
  if (!$('playlist')) return;
  $('playlist').innerHTML = appState.playlist.map((file, i) =>
    `<div class="file-item"><span>${safeText(file)}</span><button class="btn btn-danger" onclick="removeFromPlaylist(${i})">حذف</button></div>`
  ).join('');
}

function removeFromPlaylist(index) {
  appState.playlist.splice(index, 1);
  renderPlaylist();
}

function playPlaylist() {
  apiPost('/api/audio/playlist', {
    files: appState.playlist.join(','),
    volume: $('playlistVolume')?.value || 15,
    respectAdhan: $('playlistAdhanRespect')?.checked ? '1' : '0'
  }).catch((err) => toast(`فشل التشغيل: ${err.message}`));
}

function stopPlaylist() {
  stopAudio();
}

function clearPlaylist() {
  appState.playlist = [];
  renderPlaylist();
}

function saveMaghribOffset() {
  apiPost('/api/maghrib/offset', { offset: $('maghribOffset')?.value || 1 })
    .then(() => toast('تم حفظ تنبيه المغرب')).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadMaghribAlerts() {
  apiGet('/api/maghrib/alerts', []).then((data) => {
    const alerts = Array.isArray(data) ? data : (data.alerts || []);
    if (!$('maghribAlerts')) return;
    $('maghribAlerts').innerHTML = alerts.map((a) =>
      `<div class="file-item"><span>اليوم ${a.day}: ${safeText(a.file || 'بدون ملف')}</span><span>${a.enabled ? 'مفعل' : 'متوقف'}</span></div>`
    ).join('');
  });
}

function saveMaghribAlerts() {
  toast('استخدم تعيين الملفات من بطاقة الملفات ثم احفظ الإعدادات المتقدمة لاحقاً');
}

function startOTA() {
  const input = $('otaFile');
  if (!input?.files?.length) return toast('اختر ملف التحديث');
  const body = new FormData();
  body.append('update', input.files[0]);
  fetch('/api/ota', { method: 'POST', body })
    .then((r) => {
      if (!r.ok) throw new Error('ota failed');
      toast('تم رفع التحديث. سيعاد تشغيل الجهاز.');
    })
    .catch((err) => toast(`فشل التحديث: ${err.message}`));
}

function uploadCSV() {
  const input = $('csvFileInput');
  if (!input?.files?.length) return toast('اختر ملف CSV');
  const body = new FormData();
  body.append('month', $('csvMonthSelect')?.value || '1');
  body.append('file', input.files[0]);
  fetch(`/api/csv/upload?month=${encodeURIComponent($('csvMonthSelect')?.value || '1')}`, { method: 'POST', body })
    .then((r) => {
      if (!r.ok) throw new Error('csv upload failed');
      toast('تم رفع ملف CSV');
      loadCsvStatus();
    })
    .catch((err) => toast(`فشل الرفع: ${err.message}`));
}

function toggleCSVMode() {
  apiPost('/api/csv/toggle', { enabled: $('csvModeToggle')?.checked ? '1' : '0' })
    .then(loadCsvStatus).catch((err) => toast(`فشل التحديث: ${err.message}`));
}

function loadCsvStatus() {
  apiGet('/api/csv/status', {}).then((data) => {
    if ($('csvModeToggle')) $('csvModeToggle').checked = !!data.enabled;
  });
}

function toggleStartupAlert() {
  saveStartupSettings();
}

function saveStartupSettings() {
  apiPost('/api/startup/save', {
    enabled: $('startupAlertEnabled')?.checked ? '1' : '0',
    file: $('startupFileSelect')?.value || ''
  }).then(() => toast('تم حفظ إعدادات بدء التشغيل'))
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadStartupSettings() {
  apiGet('/api/startup/status', {}).then((data) => {
    if ($('startupAlertEnabled')) $('startupAlertEnabled').checked = !!data.enabled;
    if ($('startupFileSelect') && data.file) $('startupFileSelect').value = data.file;
  });
}

function changePassword() {
  const oldPassword = $('oldPassword')?.value || '';
  const newPassword = $('newPassword')?.value || '';
  const confirmPassword = $('confirmPassword')?.value || '';
  if (newPassword.length < 4) return toast('كلمة المرور قصيرة');
  if (newPassword !== confirmPassword) return toast('تأكيد كلمة المرور غير مطابق');
  apiPost('/api/password/change', { old: oldPassword, password: newPassword })
    .then((data) => {
      if (!data.ok) return toast('كلمة المرور القديمة غير صحيحة');
      appState.password = newPassword;
      localStorage.setItem('vivoPassword', newPassword);
      toast('تم تغيير كلمة المرور');
    })
    .catch((err) => toast(`فشل التغيير: ${err.message}`));
}

document.addEventListener('DOMContentLoaded', () => {
  toggleDHCP();
  toggleScheduleFields();
  toggleLoopFields();
  populateGpioPins();
  if ($('scheduleVolume')) {
    $('scheduleVolume').addEventListener('input', () => {
      if ($('scheduleVolumeValue')) $('scheduleVolumeValue').textContent = $('scheduleVolume').value;
    });
  }
  setInterval(() => {
    if ($('mainContent')?.style.display !== 'none') {
      updateClock();
      fetchStatus();
    }
  }, 5000);
});
