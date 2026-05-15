const DEFAULT_PASSWORD = "1234";
let isLoggedIn = false;

// Tab navigation
function showTab(tabName) {
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  let el = document.getElementById('tab-' + tabName);
  if (el) el.classList.add('active');
}

// Login
function doLogin() {
  const pass = document.getElementById('loginPassword').value;
  if (pass === DEFAULT_PASSWORD) {
    isLoggedIn = true;
    document.getElementById('loginOverlay').style.display = 'none';
    document.getElementById('mainContent').style.display = 'block';
    initializeUI();
  } else {
    document.getElementById('loginError').style.display = 'block';
  }
}

// Global error handling
function safeFetch(url, options = {}) {
  return fetch(url, options)
    .catch(err => {
      console.warn(`❌ Fetch failed: ${url}`, err);
      return { ok: false, json: () => Promise.resolve({}) };
    });
}

// Page initialization
document.addEventListener('DOMContentLoaded', () => {
  setTimeout(() => {
    if (!isLoggedIn) {
      document.getElementById('loginOverlay').style.display = 'flex';
      document.getElementById('mainContent').style.display = 'none';
    }
  }, 500);
});

function initializeUI() {
  updateClock();
  setInterval(updateClock, 1000);
  
  fetchStatus();
  setInterval(fetchStatus, 3000);
  
  loadCountries();
  loadFileList();
  loadSchedules();
  loadGPIOSchedules();
  loadMaghribAlerts();
}

// Clock Update
function updateClock() {
  fetch('/api/time')
    .then(r => r.text())
    .then(time => {
      document.getElementById('timeDisplay').textContent = time;
    })
    .catch(() => document.getElementById('timeDisplay').textContent = '--:--');
  
  fetch('/api/date')
    .then(r => r.json())
    .then(data => {
      if (data.greg) document.getElementById('gregDate').textContent = data.greg;
      if (data.hijri) document.getElementById('hijriDate').textContent = data.hijri;
    });
}

// Status
function fetchStatus() {
  fetch('/api/status')
    .then(r => r.json())
    .then(data => {
      if (data.playing) {
        document.getElementById('playingStatus').textContent = `🔊 تشغيل: ${data.file}`;
      } else {
        document.getElementById('playingStatus').textContent = '⏸️ متوقف';
      }
      if (data.volume !== undefined) {
        document.getElementById('volumeSlider').value = data.volume;
      }
    });
  
  fetchPrayerTimes();
}

// Prayer Times
function fetchPrayerTimes() {
  fetch('/api/prayer/times')
    .then(r => r.json())
    .then(data => {
      if (data.valid) {
        document.getElementById('fajrTime').textContent = data.fajr || '--:--';
        document.getElementById('dhuhrTime').textContent = data.dhuhr || '--:--';
        document.getElementById('asrTime').textContent = data.asr || '--:--';
        document.getElementById('maghribTime').textContent = data.maghrib || '--:--';
        document.getElementById('ishaTime').textContent = data.isha || '--:--';
      }
    });
}

// WiFi Functions
function scanWiFi() {
  const btn = document.querySelector('button[onclick="scanWiFi()"]');
  if (btn) btn.disabled = true;
  
  fetch('/api/wifi/scan')
    .then(r => {
      if (r.status === 202) {
        document.getElementById('wifiList').innerHTML = '<p>🔍 جاري البحث عن الشبكات...</p>';
        setTimeout(scanWiFi, 2500);
        return;
      }
      return r.json();
    })
    .then(data => {
      if (!data || !data.networks) return;
      let html = '';
      data.networks.forEach(n => {
        html += `<div class="file-item" onclick="selectNetwork('${n.ssid}')" style="cursor:pointer">${n.ssid} <span style="font-size:12px">(${n.rssi}dBm)</span></div>`;
      });
      document.getElementById('wifiList').innerHTML = html || '<p>لا توجد شبكات</p>';
    })
    .finally(() => { if (btn) btn.disabled = false; });
}

function selectNetwork(ssid) {
  document.getElementById('ssid').value = ssid;
}

function toggleDHCP() {
  const dhcp = document.getElementById('dhcpToggle').checked;
  document.getElementById('staticIPFields').style.display = dhcp ? 'none' : 'block';
}

function saveNetwork() {
  const ssid = document.getElementById('ssid').value;
  const pass = document.getElementById('wifiPass').value;
  
  if (!ssid) {
    alert('❌ الرجاء إدخال اسم الشبكة');
    return;
  }
  
  const dhcp = document.getElementById('dhcpToggle').checked;
  const data = {ssid, pass, dhcp};
  
  if (!dhcp) {
    data.ip = document.getElementById('staticIP').value;
    data.gateway = document.getElementById('gateway').value;
    data.subnet = document.getElementById('subnet').value;
    data.dns = document.getElementById('dns').value;
  }
  
  fetch('/api/wifi/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(data)
  })
  .then(() => alert('✅ تم الحفظ - أعد تشغيل الجهاز'))
  .catch(err => alert('❌ خطأ: ' + err));
}

// File Management
function loadFileList(dir = '/') {
  fetch('/api/files/list')
    .then(r => r.json())
    .then(data => {
      let files = data.files || [];
      let html = '';
      files.forEach(f => {
        if (!f.isDirectory) {
          html += `<div class="file-item">
            <span>${f.name}</span>
            <span style="font-size:12px">${(f.size/1024/1024).toFixed(2)} MB</span>
            <button class="btn" style="font-size:12px;padding:5px 10px" onclick="playFile('${f.name}')">▶️</button>
            <button class="btn btn-danger" style="font-size:12px;padding:5px 10px" onclick="deleteFile('${f.name}')">🗑️</button>
          </div>`;
        }
      });
      document.getElementById('fileList').innerHTML = html || '<p>لا توجد ملفات</p>';
      
      // Update file selects
      updateFileSelects(files.filter(f => !f.isDirectory).map(f => f.name));
    });
}

function updateFileSelects(files) {
  const selects = [
    'fajrAdhanFileSelect', 'adhanFileSelect', 'iqamaFileSelect',
    'scheduleFile', 'inputFile', 'eidTakbeerFile', 'playlistFileSelect', 'startupFileSelect'
  ];
  
  selects.forEach(selectId => {
    const select = document.getElementById(selectId);
    if (select) {
      const current = select.value;
      select.innerHTML = '<option value="">اختر ملف</option>';
      files.forEach(f => {
        select.innerHTML += `<option value="${f}">${f}</option>`;
      });
      select.value = current;
    }
  });
}

function uploadFile() {
  const file = document.getElementById('fileInput').files[0];
  if (!file) {
    alert('الرجاء اختيار ملف');
    return;
  }
  
  const formData = new FormData();
  formData.append('file', file);
  
  fetch('/api/files/upload', {
    method: 'POST',
    body: formData
  })
  .then(r => {
    if (r.ok) {
      alert('✅ تم رفع الملف');
      loadFileList();
    } else {
      alert('❌ خطأ في الرفع');
    }
  })
  .catch(err => alert('❌ خطأ: ' + err));
}

function playFile(filename) {
  fetch('/api/audio/play', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({file: filename, priority: 1, volume: 20})
  })
  .then(() => fetchStatus())
  .catch(err => alert('❌ خطأ: ' + err));
}

function stopAudio() {
  fetch('/api/audio/stop', {method: 'POST'})
    .then(() => fetchStatus());
}

function setVolume(val) {
  fetch('/api/audio/volume', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({volume: parseInt(val)})
  });
}

function deleteFile(filename) {
  if (confirm(`هل تريد حذف ${filename}؟`)) {
    fetch('/api/files/delete', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({file: filename})
    })
    .then(() => {
      alert('✅ تم الحذف');
      loadFileList();
    });
  }
}

function createFolder() {
  const name = document.getElementById('newFolderName').value;
  if (!name) {
    alert('الرجاء إدخال اسم المجلد');
    return;
  }
  fetch('/api/files/mkdir', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({folder: name})
  })
  .then(() => {
    alert('✅ تم إنشاء المجلد');
    loadFileList();
  });
}

// Countries and Prayer
function loadCountries() {
  fetch('/api/location/countries')
    .then(r => r.json())
    .then(data => {
      if (!Array.isArray(data)) return;
      let html = '<option value="">اختر الدولة</option>';
      data.forEach(c => {
        html += `<option value="${c}">${c}</option>`;
      });
      document.getElementById('countrySelect').innerHTML = html;
    });
}

function onCountryChange() {
  const country = document.getElementById('countrySelect').value;
  if (!country) return;
  
  fetch(`/api/location/cities?country=${encodeURIComponent(country)}`)
    .then(r => r.json())
    .then(data => {
      if (!Array.isArray(data)) return;
      let html = '<option value="">اختر المدينة</option>';
      data.forEach(c => {
        html += `<option value="${c}">${c}</option>`;
      });
      document.getElementById('citySelect').innerHTML = html;
    });
}

function fetchPrayerTimes() {
  const country = document.getElementById('countrySelect').value;
  const city = document.getElementById('citySelect').value;
  
  if (!country || !city) return;
  
  fetch('/api/prayer/calculate', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({country, city})
  })
  .then(r => r.json())
  .then(data => {
    if (data.valid) {
      document.getElementById('fajrTime').textContent = data.fajr;
      document.getElementById('dhuhrTime').textContent = data.dhuhr;
      document.getElementById('asrTime').textContent = data.asr;
      document.getElementById('maghribTime').textContent = data.maghrib;
      document.getElementById('ishaTime').textContent = data.isha;
    }
  });
}

function saveOffsets() {
  const offsets = {
    fajr: parseInt(document.getElementById('offsetFajr').value) || 0,
    dhuhr: parseInt(document.getElementById('offsetDhuhr').value) || 0,
    asr: parseInt(document.getElementById('offsetAsr').value) || 0,
    maghrib: parseInt(document.getElementById('offsetMaghrib').value) || 0,
    isha: parseInt(document.getElementById('offsetIsha').value) || 0
  };
  
  fetch('/api/prayer/offsets', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(offsets)
  })
  .then(() => alert('✅ تم حفظ الإزاحات'));
}

// Scheduler Functions
function toggleScheduleFields() {
  const type = document.getElementById('scheduleType').value;
  let html = '';
  
  if (type === 'weekly') {
    html = '<label>اليوم</label><select id="scheduleDay"><option value="0">الأحد</option><option value="1">الاثنين</option><option value="2">الثلاثاء</option><option value="3">الأربعاء</option><option value="4">الخميس</option><option value="5">الجمعة</option><option value="6">السبت</option></select>';
  } else if (type === 'monthly') {
    html = '<label>اليوم من الشهر</label><input type="number" id="scheduleDayOfMonth" min="1" max="31" value="1">';
  } else if (type === 'specific') {
    html = '<label>التاريخ</label><input type="date" id="scheduleDate">';
  } else if (type === 'prayer_relative') {
    html = '<label>الصلاة</label><select id="schedulePrayer"><option value="fajr">الفجر</option><option value="dhuhr">الظهر</option><option value="asr">العصر</option><option value="maghrib">المغرب</option><option value="isha">العشاء</option></select><label>الإزاحة (دقائق)</label><input type="number" id="scheduleOffset" value="0">';
  }
  
  document.getElementById('scheduleExtraFields').innerHTML = html;
}

function toggleLoopFields() {
  const loop = document.getElementById('scheduleLoopToggle').value;
  document.getElementById('loopFields').style.display = loop === 'yes' ? 'block' : 'none';
}

function addSchedule() {
  const file = document.getElementById('scheduleFile').value;
  const type = document.getElementById('scheduleType').value;
  const time = document.getElementById('scheduleTime').value;
  const volume = parseInt(document.getElementById('scheduleVolume').value);
  const loop = document.getElementById('scheduleLoopToggle').value === 'yes';
  
  if (!file || !time) {
    alert('❌ الرجاء ملء البيانات المطلوبة');
    return;
  }
  
  const schedule = {file, type, time, volume, loop};
  
  if (type === 'weekly') schedule.day = parseInt(document.getElementById('scheduleDay').value);
  else if (type === 'monthly') schedule.dayOfMonth = parseInt(document.getElementById('scheduleDayOfMonth').value);
  else if (type === 'specific') schedule.date = document.getElementById('scheduleDate').value;
  else if (type === 'prayer_relative') {
    schedule.prayer = document.getElementById('schedulePrayer').value;
    schedule.offset = parseInt(document.getElementById('scheduleOffset').value);
  }
  
  if (loop) schedule.loopDuration = parseInt(document.getElementById('scheduleLoopDuration').value);
  
  fetch('/api/scheduler/add', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(schedule)
  })
  .then(() => {
    alert('✅ تم إضافة الجدولة');
    loadSchedules();
  });
}

function loadSchedules() {
  fetch('/api/scheduler/list')
    .then(r => r.json())
    .then(data => {
      let html = '';
      if (Array.isArray(data)) {
        data.forEach((s, idx) => {
          html += `<li>${s.file} - ${s.time} <button onclick="deleteSchedule(${idx})">🗑️</button></li>`;
        });
      }
      document.getElementById('scheduleList').innerHTML = html;
    });
}

function deleteSchedule(idx) {
  fetch(`/api/scheduler/delete/${idx}`, {method: 'POST'})
    .then(() => loadSchedules());
}

// GPIO Functions
function saveInputMapping() {
  const pin = document.getElementById('inputPin').value;
  const file = document.getElementById('inputFile').value;
  
  if (!file) {
    alert('❌ الرجاء اختيار ملف');
    return;
  }
  
  fetch('/api/gpio/input/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({pin: parseInt(pin), file})
  })
  .then(() => alert('✅ تم الحفظ'));
}

function saveOutputMapping() {
  const pin = document.getElementById('outputPin').value;
  const duration = document.getElementById('outputDuration').value;
  
  fetch('/api/gpio/output/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({pin: parseInt(pin), duration: parseInt(duration)})
  })
  .then(() => alert('✅ تم الحفظ'));
}

function addGpioSchedule() {
  const pin = document.getElementById('gpioSchedPin').value;
  const start = document.getElementById('gpioSchedStart').value;
  const end = document.getElementById('gpioSchedEnd').value;
  
  if (!pin || !start || !end) {
    alert('❌ الرجاء ملء البيانات');
    return;
  }
  
  fetch('/api/gpio/schedule/add', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({pin: parseInt(pin), start, end})
  })
  .then(() => {
    alert('✅ تم الحفظ');
    loadGPIOSchedules();
  });
}

function loadGPIOSchedules() {
  fetch('/api/gpio/schedule/list')
    .then(r => r.json())
    .then(data => {
      if (Array.isArray(data) && data.length > 0) {
        document.getElementById('gpioSchedPin').innerHTML = '<option value="">اختر</option>' + 
          data.map(s => `<option value="${s.pin}">GPIO ${s.pin}</option>`).join('');
      }
    });
}

// Eid Mode
function toggleEidMode() {
  const enabled = document.getElementById('eidModeToggle').checked;
  fetch('/api/eid/toggle', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({enabled})
  });
}

function triggerTakbeer() {
  const file = document.getElementById('eidTakbeerFile').value;
  if (!file) {
    alert('❌ الرجاء اختيار ملف');
    return;
  }
  
  fetch('/api/eid/play', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({file})
  })
  .then(() => fetchStatus());
}

function saveEidFile() {
  const file = document.getElementById('eidTakbeerFile').value;
  fetch('/api/eid/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({file})
  })
  .then(() => alert('✅ تم الحفظ'));
}

// Player Functions
function addToPlaylist() {
  const file = document.getElementById('playlistFileSelect').value;
  if (!file) {
    alert('❌ الرجاء اختيار ملف');
    return;
  }
  
  fetch('/api/player/playlist/add', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({file})
  })
  .then(() => {
    alert('✅ تم إضافة الملف');
    loadPlaylist();
  });
}

function loadPlaylist() {
  fetch('/api/player/playlist')
    .then(r => r.json())
    .then(data => {
      let html = '';
      if (Array.isArray(data)) {
        data.forEach((f, idx) => {
          html += `<div class="file-item">${f} <button onclick="removeFromPlaylist(${idx})">❌</button></div>`;
        });
      }
      document.getElementById('playlist').innerHTML = html;
    });
}

function removeFromPlaylist(idx) {
  fetch(`/api/player/playlist/remove/${idx}`, {method: 'POST'})
    .then(() => loadPlaylist());
}

function playPlaylist() {
  const volume = parseInt(document.getElementById('playlistVolume').value);
  const respectAdhan = document.getElementById('playlistAdhanRespect').checked;
  
  fetch('/api/player/play', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({volume, respectAdhan})
  });
}

function stopPlaylist() {
  fetch('/api/audio/stop', {method: 'POST'});
}

function clearPlaylist() {
  fetch('/api/player/playlist/clear', {method: 'POST'})
    .then(() => loadPlaylist());
}

// Maghrib Alerts
function saveMaghribOffset() {
  const offset = parseInt(document.getElementById('maghribOffset').value);
  fetch('/api/maghrib/offset', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({offset})
  })
  .then(() => alert('✅ تم الحفظ'));
}

function loadMaghribAlerts() {
  fetch('/api/maghrib/alerts')
    .then(r => r.json())
    .then(data => {
      let html = '';
      if (Array.isArray(data)) {
        data.forEach(a => {
          html += `<label><input type="checkbox" checked> ${a}</label><br>`;
        });
      }
      document.getElementById('maghribAlerts').innerHTML = html || '<p>لا توجد تنبيهات</p>';
    });
}

function saveMaghribAlerts() {
  alert('✅ تم الحفظ');
}

// Manual Prayer Times
function toggleManualMode() {
  const enabled = document.getElementById('manualModeToggle').checked;
  fetch('/api/prayer/manual/toggle', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({enabled})
  });
}

function loadManualSettings() {
  fetch('/api/prayer/manual/settings')
    .then(r => r.json())
    .then(data => {
      if (data.fajr) document.getElementById('manFajr').value = data.fajr;
      if (data.dhuhr) document.getElementById('manDhuhr').value = data.dhuhr;
      if (data.asr) document.getElementById('manAsr').value = data.asr;
      if (data.maghrib) document.getElementById('manMaghrib').value = data.maghrib;
      if (data.isha) document.getElementById('manIsha').value = data.isha;
    });
}

function saveManualPrayerTimes() {
  const times = {
    fajr: document.getElementById('manFajr').value,
    dhuhr: document.getElementById('manDhuhr').value,
    asr: document.getElementById('manAsr').value,
    maghrib: document.getElementById('manMaghrib').value,
    isha: document.getElementById('manIsha').value
  };
  
  fetch('/api/prayer/manual/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(times)
  })
  .then(() => alert('✅ تم الحفظ'));
}

// OTA Update
function startOTA() {
  const file = document.getElementById('otaFile').files[0];
  if (!file) {
    alert('الرجاء اختيار ملف التحديث');
    return;
  }
  
  const formData = new FormData();
  formData.append('file', file);
  
  fetch('/api/system/ota', {
    method: 'POST',
    body: formData
  })
  .then(() => alert('✅ جاري التحديث...'));
}

// CSV
function uploadCSV() {
  const file = document.getElementById('csvFileInput').files[0];
  const month = document.getElementById('csvMonthSelect').value;
  
  if (!file) {
    alert('❌ الرجاء اختيار ملف CSV');
    return;
  }
  
  const formData = new FormData();
  formData.append('file', file);
  formData.append('month', month);
  
  fetch('/api/csv/upload', {
    method: 'POST',
    body: formData
  })
  .then(() => {
    alert('✅ تم رفع CSV');
  });
}

function toggleCSVMode() {
  const enabled = document.getElementById('csvModeToggle').checked;
  fetch('/api/csv/toggle', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({enabled})
  });
}

// Startup Alert
function toggleStartupAlert() {
  const enabled = document.getElementById('startupAlertEnabled').checked;
  fetch('/api/startup/toggle', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({enabled})
  });
}

function saveStartupSettings() {
  const file = document.getElementById('startupFileSelect').value;
  fetch('/api/startup/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({file})
  })
  .then(() => alert('✅ تم الحفظ'));
}

// Password
function changePassword() {
  const old = document.getElementById('oldPassword').value;
  const newPass = document.getElementById('newPassword').value;
  const confirm = document.getElementById('confirmPassword').value;
  
  if (old !== DEFAULT_PASSWORD) {
    alert('❌ كلمة المرور القديمة غير صحيحة');
    return;
  }
  
  if (newPass !== confirm) {
    alert('❌ كلمات المرور غير متطابقة');
    return;
  }
  
  // In production, this should be sent to server
  alert('✅ تم تغيير كلمة المرور (يجب حفظها في الجهاز)');
  document.getElementById('oldPassword').value = '';
  document.getElementById('newPassword').value = '';
  document.getElementById('confirmPassword').value = '';
}