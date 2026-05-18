const appState = {
  files: [],
  countries: [],
  cities: [],
  playlist: [],
  password: localStorage.getItem('vivoPassword') || 'admin'
};

const $ = (id) => document.getElementById(id);

let currentDir = '/';

function toast(message) {
  let container = $('toast-container');
  if (!container) {
    container = document.createElement('div');
    container.id = 'toast-container';
    container.style.cssText = 'position:fixed;bottom:20px;left:50%;transform:translateX(-50%);z-index:9999;display:flex;flex-direction:column;gap:10px;pointer-events:none;';
    document.body.appendChild(container);
    
    // Keyframes are in style.css
  }
  const el = document.createElement('div');
  el.style.cssText = 'background:#2c3e50;color:#fff;padding:12px 24px;border-radius:8px;box-shadow:0 4px 12px rgba(0,0,0,0.3);font-size:15px;animation:fadein 0.3s, fadeout 0.3s 4.7s forwards; border-left: 4px solid #3498db; text-align:center; direction:rtl;';
  el.innerHTML = message;
  container.appendChild(el);
  setTimeout(() => { if (el.parentNode) el.parentNode.removeChild(el); }, 5000);
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

function apiGet(url, fallback = {}, retries = 3) {
  return fetch(url)
    .then((response) => {
      if (!response.ok) throw new Error(`${response.status} ${url}`);
      return response.json();
    })
    .catch((err) => {
      console.warn('GET failed:', url, err);
      if (retries > 0) {
        return new Promise((r) => setTimeout(r, 2000)).then(() => apiGet(url, fallback, retries - 1));
      }
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

  if (tabName === 'files') { loadFileList(); }
  if (tabName === 'scheduler') { loadSchedules(); }
  if (tabName === 'gpio') { loadGpioMappings(); }
  if (tabName === 'maghrib') { loadMaghribAlerts(); }
  if (tabName === 'network') { loadWifiStatus(); }
  if (tabName === 'prayer') { loadCountries(); loadManualSettings(); loadCsvStatus(); }
  if (tabName === 'settings') { loadStartupSettings(); loadSessionTimeout(); loadManualTimeStatus(); }
  if (tabName === 'eid') { loadEidSchedules(); loadEidTakbeerConfig(); }
}

function doLogin() {
  const password = $('loginPassword')?.value || '';
  apiPost('/api/password/check', { password })
    .then((data) => {
      if (data.ok) {
        $('loginOverlay').style.display = 'none';
        $('mainContent').style.display = 'block';
        $('loginError').style.display = 'none';
        localStorage.setItem('vivoSessionTime', Date.now().toString());
        initDashboard();
      } else {
        $('loginError').style.display = 'block';
      }
    })
    .catch(() => {
      if (password === appState.password) {
        $('loginOverlay').style.display = 'none';
        $('mainContent').style.display = 'block';
        localStorage.setItem('vivoSessionTime', Date.now().toString());
        initDashboard();
      } else {
        $('loginError').style.display = 'block';
      }
    });
}

function doLogout() {
    localStorage.removeItem('vivoSessionTime');
    location.reload();
}

function forgotPassword() {
    const code = prompt('أدخل كود الاستعادة (Master Code):');
    if (!code) return;
    
    const newPass = prompt('أدخل كلمة المرور الجديدة:');
    if (!newPass || newPass.length < 4) return toast('كلمة المرور قصيرة جداً أو ملغاة');
    
    apiPost('/api/password/master_reset', { master: code, password: newPass })
        .then((data) => {
            if (data.ok) {
                toast('تم استعادة وتغيير كلمة المرور بنجاح! يرجى تسجيل الدخول');
                appState.password = newPass;
                localStorage.setItem('vivoPassword', newPass);
            } else {
                toast('كود الاستعادة غير صحيح');
            }
        }).catch(err => toast(`فشل الاتصال: ${err.message}`));
}

function togglePasswordVisibility(inputId, iconId) {
    const input = $(inputId);
    const icon = $(iconId);
    if (!input || !icon) return;
    
    if (input.type === 'password') {
        input.type = 'text';
        icon.classList.remove('fa-eye');
        icon.classList.add('fa-eye-slash');
        icon.style.color = '#3498db';
    } else {
        input.type = 'password';
        icon.classList.remove('fa-eye-slash');
        icon.classList.add('fa-eye');
        icon.style.color = '#7f8c8d';
    }
}

function systemReboot() {
    if (confirm('هل أنت متأكد من إعادة تشغيل النظام؟')) {
        apiPost('/api/system/reboot').then(() => {
            toast('جاري إعادة التشغيل...');
            setTimeout(() => location.reload(), 5000);
        });
    }
}

function systemFactoryReset() {
    if (confirm('تحذير خطير: سيتم مسح جميع الإعدادات والمواقيت والشبكات. هل أنت متأكد تماماً؟')) {
        apiPost('/api/system/reset').then(() => {
            toast('جاري ضبط المصنع ومسح الإعدادات...');
            setTimeout(() => location.reload(), 8000);
        });
    }
}

function systemShutdown() {
    if (confirm('تحذير: سيتم إغلاق النظام نهائياً ولن يعمل إلا بفصل الكهرباء وإعادتها. هل أنت متأكد؟')) {
        apiPost('/api/system/shutdown').then(() => {
            toast('تم إغلاق النظام بنجاح. افصل الكهرباء لإعادة التشغيل.');
            setTimeout(() => $('mainContent').innerHTML = '<h1 style="text-align:center; margin-top:20vh;">تم إغلاق النظام</h1>', 2000);
        });
    }
}

function initDashboard() {
  const epochSecs = Math.floor(Date.now() / 1000);
  apiPost('/api/time/sync_browser', { timestamp: epochSecs }).then(() => {
    updateClock();
    fetchPrayerTimes();
  });
  
  fetchStatus();
  loadCountries();
  loadManualSettings();
  loadFileList();
  loadSchedules();
  loadMaghribAlerts();
  loadStartupSettings();
  loadSessionTimeout();
  loadCsvStatus();
  loadWifiStatus();
  loadEidSchedules();
  loadEidTakbeerConfig();
  populateGpioPins();
  loadDDNS();
}

function format12Hour(time24) {
  if (!time24 || time24 === '--:--' || time24.indexOf(':') === -1) return time24;
  let [h, m] = time24.split(':');
  let hi = parseInt(h, 10);
  const ampm = hi >= 12 ? 'م' : 'ص';
  hi = hi % 12;
  hi = hi ? hi : 12;
  return `${String(hi).padStart(2, '0')}:${m} ${ampm}`;
}

function updateClock() {
  apiGet('/api/clock', {}).then((data) => {
    if ($('timeDisplay') && data.time) $('timeDisplay').textContent = format12Hour(data.time);
    if ($('gregDate')) $('gregDate').textContent = data.greg || '';
    if ($('hijriDate')) $('hijriDate').textContent = data.hijri || '';
  });
}

function fetchStatus() {
  apiGet('/api/status', {}).then((data) => {
    if ($('playingStatus')) {
      $('playingStatus').textContent = data.playing ? `يعمل: ${data.file || ''}` : 'متوقف';
    }
    if ($('volumeSlider') && data.volume !== undefined) {
      $('volumeSlider').value = data.volume;
      if ($('mainVolVal')) $('mainVolVal').textContent = data.volume;
    }
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
  toast('جاري الاتصال بالشبكة...');

  apiPost('/api/wifi/connect', data)
    .then((result) => {
      if (result.connected) {
        const ip = result.ip || '';
        toast(`تم الاتصال بنجاح. IP: ${ip}`);
        loadWifiStatus();
      } else {
        toast('فشل الاتصال. ستبقى نقطة VivoSmart-Setup متاحة.');
        loadWifiStatus();
      }
    })
    .catch((err) => {
      toast(`فشل الاتصال: ${err.message}`);
      loadWifiStatus();
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

    const icon = $('netIcon');
    const title = $('netTitle');
    const detail = $('netDetail');
    const ssidEl = $('netSsid');
    if (data.connected) {
      if (icon) { icon.className = 'network-icon connected'; icon.innerHTML = '<i class="fas fa-wifi"></i>'; }
      if (title) { title.className = 'network-title connected'; title.textContent = 'متصل'; }
      if (detail) detail.textContent = `IP: ${data.ip || ''}`;
      if (ssidEl) ssidEl.textContent = `الشبكة: ${data.ssid || ''}`;
    } else {
      if (icon) { icon.className = 'network-icon disconnected'; icon.innerHTML = '<i class="fas fa-wifi-slash"></i>'; }
      if (title) { title.className = 'network-title disconnected'; title.textContent = 'غير متصل'; }
      if (detail) detail.textContent = `نقطة الإعداد: ${data.apIp || '192.168.4.1'}`;
      if (ssidEl) ssidEl.textContent = data.savedSsid ? `محفوظة: ${data.savedSsid}` : '';
    }
  });
}

function selectNetwork(ssid) {
  if ($('ssid')) $('ssid').value = ssid;
}

function toggleDHCP() {
  if ($('staticIPFields')) $('staticIPFields').style.display = $('dhcpToggle')?.checked ? 'none' : 'block';
}

function loadDDNS() {
  apiGet('/api/ddns/config', {}).then((data) => {
    if ($('ddnsToggle')) {
      $('ddnsToggle').checked = data.enabled || false;
      toggleDDNS();
    }
    if ($('ddnsDomain')) $('ddnsDomain').value = data.domain || '';
    if ($('ddnsUser')) $('ddnsUser').value = data.user || '';
    if ($('ddnsPass')) $('ddnsPass').value = data.hasPass ? '********' : '';
  });
}

function toggleDDNS() {
  if ($('ddnsFields')) $('ddnsFields').style.display = $('ddnsToggle')?.checked ? 'block' : 'none';
}

function saveDDNS() {
  const enabled = $('ddnsToggle')?.checked || false;
  const domain = $('ddnsDomain')?.value || '';
  const user = $('ddnsUser')?.value || '';
  const pass = $('ddnsPass')?.value || '';
  apiPost('/api/ddns/save', { enabled, domain, user, pass }).then(() => toast('تم حفظ إعدادات DDNS'));
}

function defaultPrayerMethod(country) {
  if (country === 'Egypt') return '0';
  if (country === 'Saudi Arabia') return '2';
  return '1';
}

function loadCountries() {
  apiGet('/api/location/countries', { countries: [] }).then((data) => {
    appState.countries = data.countries || data || [];
    const select = $('countrySelect');
    if (!select) return;
    select.innerHTML = '<option value="">اختر الدولة</option>' +
      appState.countries.map((country) => `<option value="${safeAttr(country)}">${safeText(country)}</option>`).join('');
    
    // Load config after countries are populated
    apiGet('/api/prayer/config', {}).then((cfg) => {
      if (cfg.country) {
        select.value = cfg.country;
        if ($('methodSelect') && cfg.method !== undefined) {
          $('methodSelect').value = cfg.method;
        }
        if ($('offsetHijri') && cfg.hijriOffset !== undefined) {
          $('offsetHijri').value = cfg.hijriOffset;
        }
        // Fetch cities for this country and set the selected city
        apiGet(`/api/location/cities?country=${encodeURIComponent(cfg.country)}`, { cities: [] }).then((cData) => {
          appState.cities = cData.cities || cData || [];
          const cSelect = $('citySelect');
          if (cSelect) {
            cSelect.innerHTML = '<option value="">اختر المدينة</option>' +
              appState.cities.map((city) => `<option value="${safeAttr(city)}">${safeText(city)}</option>`).join('');
            if (cfg.city) cSelect.value = cfg.city;
          }
          fetchPrayerTimes(); // Automatically calculate and display
        });
      }
    });
  });
}

function onCountryChange() {
  const country = $('countrySelect')?.value || '';
  if ($('citySelect')) $('citySelect').innerHTML = '<option value="">اختر المدينة</option>';
  if ($('methodSelect') && country) $('methodSelect').value = defaultPrayerMethod(country);
  if (!country) return;
  apiGet(`/api/location/cities?country=${encodeURIComponent(country)}`, { cities: [] }).then((data) => {
    appState.cities = data.cities || data || [];
    const select = $('citySelect');
    if (!select) return;
    select.innerHTML = '<option value="">اختر المدينة</option>' +
      appState.cities.map((city) => `<option value="${safeAttr(city)}">${safeText(city)}</option>`).join('');
  });
}

function fetchPrayerTimes() {
  const countrySelect = $('countrySelect');
  const citySelect = $('citySelect');
  const country = countrySelect?.value || '';
  const city = citySelect?.value || '';
  const method = $('methodSelect')?.value || '0';
  const query = country && city ? `?country=${encodeURIComponent(country)}&city=${encodeURIComponent(city)}&method=${method}` : `?method=${method}`;

  if (country && city && $('locationDisplay')) {
    const cName = countrySelect.options[countrySelect.selectedIndex]?.text || country;
    const ciName = citySelect.options[citySelect.selectedIndex]?.text || city;
    $('locationDisplay').innerHTML = `<i class="fas fa-map-marker-alt"></i> ${safeText(cName)} - ${safeText(ciName)}`;
  }

  apiGet(`/api/prayer/times${query}`, { ok: false }).then((data) => {
    if (data.ok === false) {
      toast('تعذر حساب المواقيت: اختر دولة ومدينة صحيحتين');
      return;
    }
    if ($('fajrTime')) $('fajrTime').textContent = format12Hour(data.fajr);
    if ($('dhuhrTime')) $('dhuhrTime').textContent = format12Hour(data.dhuhr);
    if ($('asrTime')) $('asrTime').textContent = format12Hour(data.asr);
    if ($('maghribTime')) $('maghribTime').textContent = format12Hour(data.maghrib);
    if ($('ishaTime')) $('ishaTime').textContent = format12Hour(data.isha);
    
    if ($('nextPrayer') && data.next) {
       const parts = data.next.split(' ');
       if(parts.length >= 2) {
           $('nextPrayer').textContent = `الصلاة القادمة: ${parts[0]} ${format12Hour(parts[1])}`;
       } else {
           $('nextPrayer').textContent = `الصلاة القادمة: ${data.next}`;
       }
    } else if ($('nextPrayer')) {
       $('nextPrayer').textContent = '';
    }
    // Highlight next prayer in dashboard
    const nextText = data.next || '';
    ['fajr','dhuhr','asr','maghrib','isha'].forEach(name => {
      const el = $(`prayer-${name}`);
      if (el) el.classList.remove('prayer-next');
    });
    if (nextText) {
      const nextName = nextText.split(' ')[0] || '';
      const map = {'الفجر':'fajr','الظهر':'dhuhr','العصر':'asr','المغرب':'maghrib','العشاء':'isha'};
      const id = map[nextName];
      if (id) {
        const el = $(`prayer-${id}`);
        if (el) el.classList.add('prayer-next');
      }
    }
    updateClock();
  });
}

function saveOffsets() {
  apiPost('/api/prayer/offsets', {
    fajr: $('offsetFajr')?.value || 0,
    dhuhr: $('offsetDhuhr')?.value || 0,
    asr: $('offsetAsr')?.value || 0,
    maghrib: $('offsetMaghrib')?.value || 0,
    isha: $('offsetIsha')?.value || 0,
    hijriOffset: $('offsetHijri')?.value || 0
  }).then(() => {
    toast('تم حفظ الإزاحات');
    fetchPrayerTimes();
    updateClock();
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
  }).then(() => {
    toast('تم حفظ المواقيت اليدوية');
    fetchPrayerTimes();
    updateClock();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadFileList() {
  apiGet('/api/files/list', { files: [] }).then((data) => {
    renderSdStatus(data.sd || {});
    renderI2SStatus(data.i2s || {});
    appState.files = data.files || [];
    renderFileManager();
    populateFileSelects();
  });
}

function renderFileManager() {
    const folderSelect = $('uploadFolderSelect');
    if (folderSelect) {
        folderSelect.innerHTML = '<option value="/">الرئيسي (Root)</option>' + 
            appState.files.filter(f => f.isDirectory).map(f => `<option value="/${safeAttr(f.name)}">/${safeText(f.name)}</option>`).join('');
        folderSelect.value = currentDir;
    }

    let fileHtml = '';
    if (currentDir !== '/') {
        let parentDir = currentDir.substring(0, currentDir.lastIndexOf('/'));
        if (parentDir === '') parentDir = '/';
        fileHtml += `<div class="file-item" style="background:rgba(255,255,255,0.1); cursor:pointer; justify-content:center;" onclick="currentDir='${parentDir}'; renderFileManager();">
                        <span style="font-weight:bold; color:#f1c40f;"><i class="fas fa-arrow-up"></i> الرجوع للمجلد السابق (${parentDir})</span>
                     </div>`;
    }

    const currentFiles = [];
    appState.files.forEach(f => {
        let path = '/' + f.name;
        let dirPrefix = currentDir === '/' ? '/' : currentDir + '/';
        if (path.startsWith(dirPrefix)) {
            let remainder = path.substring(dirPrefix.length);
            if (!remainder.includes('/')) {
                currentFiles.push({ ...f, shortName: remainder, fullPath: f.name });
            }
        }
    });

    fileHtml += currentFiles.map((f) => {
      const name = safeText(f.shortName);
      if (f.isDirectory) {
          return `<div class="file-item" style="background:rgba(46, 204, 113, 0.1);">
                    <span style="flex-grow:1; cursor:pointer; font-weight:bold; color:#2ecc71;" onclick="currentDir='${currentDir === '/' ? '/' : currentDir + '/'}${name}'; renderFileManager();"><i class="fas fa-folder"></i> ${name}</span>
                    <button class="btn" style="background:#f39c12; margin-left: 5px;" onclick="renameFile('${safeAttr(f.fullPath)}')">تسمية</button>
                    <button class="btn btn-danger" onclick="deleteFile('${safeAttr(f.fullPath)}')">حذف</button>
                  </div>`;
      } else {
          const size = `${((f.size || 0) / 1024 / 1024).toFixed(2)} MB`;
          return `<div class="file-item">
                    <span style="flex-grow:1; cursor:pointer;" onclick="previewFile('${safeAttr(f.fullPath)}')"><i class="fas fa-file-audio"></i> ${name}</span>
                    <span style="margin-left: 10px;">${size}</span>
                    <button class="btn" style="background:#f39c12; margin-left: 5px;" onclick="renameFile('${safeAttr(f.fullPath)}')">تسمية</button>
                    <button class="btn btn-danger" onclick="deleteFile('${safeAttr(f.fullPath)}')">حذف</button>
                  </div>`;
      }
    }).join('');

    if ($('fileList')) $('fileList').innerHTML = fileHtml || '<p style="text-align:center; opacity:0.7;">المجلد فارغ</p>';
}

function toggleFileList() {
    const container = $('fileListContainer');
    const btn = $('btnToggleFiles');
    if (!container || !btn) return;
    if (container.style.display === 'none') {
        container.style.display = 'block';
        btn.innerHTML = '<i class="fas fa-eye-slash"></i> إخفاء الملفات';
    } else {
        container.style.display = 'none';
        btn.innerHTML = '<i class="fas fa-eye"></i> عرض الملفات';
    }
}

function renameFile(oldName) {
    const newName = prompt(`تغيير اسم: ${oldName}\nأدخل الاسم الجديد:`, oldName);
    if (!newName || newName === oldName) return;
    
    apiPost('/api/files/rename', { old: oldName, new: newName })
        .then(() => loadFileList())
        .catch(err => toast(`فشل إعادة التسمية: ${err.message}`));
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
    `<div class="file-item"><span>الحجم</span><span>${sd.usedMB || 0} MB مستخدم / ${sd.totalMB || 0} MB</span></div>`
  ].join('');
}

function renderI2SStatus(i2s) {
  if (!$('i2sStatus')) return;
  if (i2s.ready) {
    $('i2sStatus').innerHTML = '<div class="file-item" style="border-left: 4px solid #2ecc71;"><span>الحالة</span><span style="color:#2ecc71; font-weight:bold;">متصل ويعمل <i class="fas fa-check-circle"></i></span></div>';
  } else {
    $('i2sStatus').innerHTML = '<div class="file-item" style="border-left: 4px solid #e74c3c;"><span>الحالة</span><span style="color:#e74c3c; font-weight:bold;">خطأ في التهيئة <i class="fas fa-times-circle"></i></span></div>';
  }
}

function populateFileSelects() {
  const options = appState.files
    .filter((f) => !f.isDirectory)
    .map((f) => `<option value="${safeAttr(f.name)}">${safeText(f.name)}</option>`)
    .join('');
  [
    'fajrAdhanFileSelect', 'adhanFileSelect', 'iqamaFileSelect', 'scheduleFile',
    'inputFile', 'eidTakbeerFile', 'eidScheduleFile', 'playlistFileSelect', 'startupFileSelect',
    'playlistFiles'
  ].forEach((id) => { if ($(id)) $(id).innerHTML = options; });
}

function uploadFile() {
  const input = $('fileInput');
  if (!input?.files?.length) return toast('اختر ملفاً أولاً');
  
  const file = input.files[0];
  const folder = $('uploadFolderSelect')?.value || '/';
  
  const formData = new FormData();
  formData.append('file', file, file.name);

  const progressBar = $('uploadProgressBar');
  const statusDiv = $('uploadStatus');
  
  if (progressBar) {
      progressBar.style.display = 'block';
      progressBar.value = 0;
  }
  if (statusDiv) {
      statusDiv.style.color = 'inherit';
      statusDiv.textContent = 'جاري الرفع... 0%';
  }

  const xhr = new XMLHttpRequest();
  xhr.open('POST', `/api/files/upload`, true);
  xhr.setRequestHeader('X-Folder', encodeURIComponent(folder));

  xhr.upload.onprogress = function(e) {
      if (e.lengthComputable) {
          const percentComplete = Math.round((e.loaded / e.total) * 100);
          if (progressBar) progressBar.value = percentComplete;
          if (statusDiv) statusDiv.textContent = `جاري الرفع... ${percentComplete}%`;
      }
  };

  xhr.onload = function() {
      if (xhr.status === 200) {
          if (statusDiv) {
              statusDiv.style.color = '#2ecc71';
              statusDiv.textContent = 'تم الرفع بنجاح!';
          }
          input.value = ''; // Clear input
          loadFileList();
      } else {
          if (statusDiv) {
              statusDiv.style.color = '#e74c3c';
              statusDiv.textContent = 'فشل الرفع. الخادم أرجع خطأ: ' + xhr.status;
          }
      }
      setTimeout(() => { if (progressBar) progressBar.style.display = 'none'; }, 5000);
  };

  xhr.onerror = function() {
      if (statusDiv) {
          statusDiv.style.color = '#e74c3c';
          statusDiv.textContent = 'حدث خطأ في الاتصال أثناء الرفع.';
      }
      if (progressBar) progressBar.style.display = 'none';
  };

  xhr.send(formData);
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
  if (type === 'weekly') html = '<label>اختر الأيام</label><div id="weeklyDays">' +
    dayNames.map((d, i) => `<label style="display:inline-flex;align-items:center;gap:4px;margin:4px 8px 4px 0;font-size:var(--fs-small)"><input type="checkbox" class="weekly-day-cb" value="${i}"> ${d}</label>`
  ).join('') + '</div>';
  if (type === 'monthly') html = '<label>اليوم من الشهر</label><input type="number" id="scheduleDay" min="1" max="31" value="1">';
  if (type === 'specific') html = '<label>التاريخ</label><input type="date" id="scheduleDate">';
  if (type === 'prayer_relative') html = '<label>الصلاة</label><select id="schedulePrayer"><option value="0">الفجر</option><option value="1">الظهر</option><option value="2">العصر</option><option value="3">المغرب</option><option value="4">العشاء</option></select><label>الإزاحة بالدقائق</label><input type="number" id="scheduleOffset" value="0">';
  if ($('scheduleExtraFields')) $('scheduleExtraFields').innerHTML = html;
}

function toggleLoopFields() {
  if ($('loopFields')) $('loopFields').style.display = $('scheduleLoopToggle')?.value === 'yes' ? 'block' : 'none';
}

function getSelectedDaysBitmask() {
  const cbs = document.querySelectorAll('.weekly-day-cb:checked');
  if (cbs.length === 0) return -1;
  let mask = 0;
  cbs.forEach(cb => { mask |= (1 << parseInt(cb.value)); });
  return mask;
}

function formatDays(mask) {
  if (mask < 0) return '';
  if (mask >= 0 && mask <= 6) return dayNames[mask];
  const days = [];
  for (let i = 0; i < 7; i++) {
    if (mask & (1 << i)) days.push(dayNames[i]);
  }
  return days.join('، ');
}

function addPlaylistSchedule() {
  const select = $('playlistFiles');
  if (!select || select.selectedOptions.length === 0) {
    return toast('يجب اختيار ملف واحد على الأقل للقائمة');
  }
  const files = Array.from(select.selectedOptions).map(opt => opt.value).join(',');
  
  const [hour = '0', minute = '0'] = ($('scheduleTime')?.value || '00:00').split(':');
  const type = $('scheduleType')?.value || 'daily';
  let dayOfWeek = -1, dayOfMonth = -1;
  if (type === 'weekly') dayOfWeek = getSelectedDaysBitmask();
  if (type === 'monthly') dayOfMonth = $('scheduleDay')?.value || '-1';
  apiPost('/api/scheduler/add', {
    file: files,
    type,
    hour,
    minute,
    dayOfWeek,
    dayOfMonth,
    specificDate: $('scheduleDate')?.value || '',
    volume: $('scheduleVolume')?.value || 20,
    loop: $('scheduleLoopToggle')?.value === 'yes' ? $('scheduleLoopDuration')?.value || 0 : 0,
    prayerIndex: $('schedulePrayer')?.value || 0,
    offsetSeconds: Number($('scheduleOffset')?.value || 0) * 60,
    eidOnly: '0'
  }).then(() => {
    toast('تم إضافة القائمة للجدولة بنجاح');
    loadSchedules();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function addSchedule(eidOnly) {
  const [hour = '0', minute = '0'] = ($('scheduleTime')?.value || '00:00').split(':');
  const type = $('scheduleType')?.value || 'daily';
  let dayOfWeek = -1, dayOfMonth = -1;
  if (type === 'weekly') dayOfWeek = getSelectedDaysBitmask();
  if (type === 'monthly') dayOfMonth = $('scheduleDay')?.value || '-1';
  apiPost('/api/scheduler/add', {
    file: $('scheduleFile')?.value || '',
    type,
    hour,
    minute,
    dayOfWeek,
    dayOfMonth,
    specificDate: $('scheduleDate')?.value || '',
    volume: $('scheduleVolume')?.value || 20,
    loop: $('scheduleLoopToggle')?.value === 'yes' ? $('scheduleLoopDuration')?.value || 0 : 0,
    prayerIndex: $('schedulePrayer')?.value || 0,
    offsetSeconds: Number($('scheduleOffset')?.value || 0) * 60,
    eidOnly: eidOnly ? '1' : '0'
  }).then(() => {
    toast('تم حفظ التنبيه');
    loadSchedules();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadSchedules() {
  apiGet('/api/scheduler/list', []).then((data) => {
    const alerts = Array.isArray(data) ? data : (data.alerts || []);
    if (!$('scheduleList')) return;
    $('scheduleList').innerHTML = alerts.map((a, i) => {
      let info = `${safeText(a.file)} - ${safeText(a.type)}`;
      if (a.type === 'weekly' && a.dayOfWeek > 0) info += ` (${formatDays(a.dayOfWeek)})`;
      info += ` ${String(a.hour).padStart(2, '0')}:${String(a.minute).padStart(2, '0')}`;
      if (a.eidOnly) info += ' <span style="color:#f1c40f">🕌 العيد</span>';
      return `<li class="file-item"><span>${info}</span><button class="btn btn-danger" onclick="deleteSchedule(${i})">حذف</button></li>`;
    }).join('');
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

function playSingleFile() {
  const file = $('playlistFileSelect')?.value || '';
  if (!file) return toast('اختر ملفاً أولاً');
  const volume = $('playlistVolume')?.value || 15;
  apiPost('/api/audio/play', { file: file, priority: 1, volume: volume })
    .catch((err) => toast(`فشل التشغيل: ${err.message}`));
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
    $('maghribAlerts').innerHTML = dayNames.map((d, i) => {
      const a = alerts[i] || {file:'', enabled:false, volume:15, loop:0};
      return `<div class="eid-alert-row" style="background:rgba(255,255,255,0.06); padding:15px; border-radius:12px; margin-bottom:15px; display:flex; flex-direction:column; gap:10px;">
        <div style="display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid rgba(255,255,255,0.1); padding-bottom:10px;">
          <span class="day-label" style="font-size:1.2em; font-weight:bold; color:#f1c40f;"><i class="fas fa-calendar-day"></i> ${d}</span>
          <label class="switch" style="margin:0;"><input type="checkbox" id="maghribEnabled_${i}" ${a.enabled?'checked':''}><span class="slider"></span></label>
        </div>
        <div style="display:flex; flex-direction:column; gap:5px;">
          <label style="font-size:0.9em; opacity:0.8;">الملف الصوتي</label>
          <select id="maghribFile_${i}" style="width:100%;">${appState.files.filter(f => !f.isDirectory).map(f => `<option value="${safeAttr(f.name)}" ${f.name === a.file ? 'selected' : ''}>${safeText(f.name)}</option>`).join('')}</select>
        </div>
        <div style="display:flex; align-items:center; gap:10px;">
          <label style="font-size:0.9em; opacity:0.8; white-space:nowrap;">مستوى الصوت</label>
          <input type="range" id="maghribVolume_${i}" value="${a.volume||15}" min="0" max="30" oninput="$('magVolVal_${i}').textContent=this.value" style="width:100%">
          <span id="magVolVal_${i}" style="font-weight:bold; width:30px; text-align:center;">${a.volume||15}</span>
        </div>
      </div>`;
    }).join('');
  });
}

function saveMaghribAlerts() {
  const alerts = dayNames.map((_, i) => ({
    day: i,
    file: $(`maghribFile_${i}`)?.value || '',
    enabled: $(`maghribEnabled_${i}`)?.checked || false,
    volume: parseInt($(`maghribVolume_${i}`)?.value || '15')
  }));
  apiPost('/api/maghrib/alert/save', {json: JSON.stringify(alerts)})
    .then(() => toast('تم حفظ تنبيهات المغرب'))
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
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

const dayNames = ['الأحد', 'الإثنين', 'الثلاثاء', 'الأربعاء', 'الخميس', 'الجمعة', 'السبت'];

const prayerNames = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'];

function toggleEidScheduleFields() {
  const type = $('eidScheduleType')?.value || 'daily';
  let html = '';
  if (type === 'weekly') html = '<label>اختر الأيام</label><div id="eidWeeklyDays">' +
    dayNames.map((d, i) => `<label style="display:inline-flex;align-items:center;gap:4px;margin:4px 8px 4px 0;font-size:var(--fs-small)"><input type="checkbox" class="eid-weekly-day-cb" value="${i}"> ${d}</label>`
  ).join('') + '</div>';
  if (type === 'monthly') html = '<label>اليوم من الشهر</label><input type="number" id="eidScheduleDay" min="1" max="31" value="1">';
  if (type === 'specific') html = '<label>التاريخ</label><input type="date" id="eidScheduleDate">';
  if (type === 'prayer_relative') html = '<label>الصلاة</label><select id="eidSchedulePrayer"><option value="0">الفجر</option><option value="1">الظهر</option><option value="2">العصر</option><option value="3">المغرب</option><option value="4">العشاء</option></select><label>الإزاحة بالدقائق</label><input type="number" id="eidScheduleOffset" value="0"><label>قبل/بعد</label><select id="eidScheduleBeforeAfter"><option value="before">قبل الصلاة</option><option value="after">بعد الصلاة</option></select>';
  if ($('eidScheduleExtraFields')) $('eidScheduleExtraFields').innerHTML = html;
}

function toggleEidLoopFields() {
  if ($('eidLoopFields')) $('eidLoopFields').style.display = $('eidScheduleLoopToggle')?.value === 'yes' ? 'block' : 'none';
}

function addEidSchedule() {
  const [hour = '0', minute = '0'] = ($('eidScheduleTime')?.value || '00:00').split(':');
  const type = $('eidScheduleType')?.value || 'daily';
  let dayOfWeek = -1, dayOfMonth = -1;
  if (type === 'weekly') {
    const cbs = document.querySelectorAll('.eid-weekly-day-cb:checked');
    if (cbs.length === 0) return toast('اختر يوماً واحداً على الأقل');
    dayOfWeek = 0;
    cbs.forEach(cb => { dayOfWeek |= (1 << parseInt(cb.value)); });
  }
  if (type === 'monthly') dayOfMonth = $('eidScheduleDay')?.value || '-1';
  let offsetSeconds = Number($('eidScheduleOffset')?.value || 0) * 60;
  if ($('eidScheduleBeforeAfter')?.value === 'before') offsetSeconds = -Math.abs(offsetSeconds);
  apiPost('/api/scheduler/add', {
    file: $('eidScheduleFile')?.value || '',
    type,
    hour,
    minute,
    dayOfWeek,
    dayOfMonth,
    specificDate: $('eidScheduleDate')?.value || '',
    volume: $('eidScheduleVolume')?.value || 20,
    loop: $('eidScheduleLoopToggle')?.value === 'yes' ? $('eidScheduleLoopDuration')?.value || 0 : 0,
    prayerIndex: $('eidSchedulePrayer')?.value || 0,
    offsetSeconds,
    eidOnly: '1'
  }).then(() => {
    toast('تم إضافة تنبيه العيد');
    loadEidSchedules();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadEidSchedules() {
  apiGet('/api/scheduler/list', []).then((data) => {
    const alerts = Array.isArray(data) ? data : (data.alerts || []);
    const eidAlerts = alerts.filter(a => a.eidOnly);
    if (!$('eidScheduleList')) return;
    $('eidScheduleList').innerHTML = eidAlerts.length
      ? eidAlerts.map((a, i) => {
          const realIndex = alerts.indexOf(a);
          let info = `${safeText(a.file)} - ${safeText(a.type)}`;
          if (a.type === 'weekly' && a.dayOfWeek > 0) info += ` (${formatDays(a.dayOfWeek)})`;
          if (a.type === 'prayer_relative') {
            const pn = prayerNames[a.prayerIndex] || '';
            info += ` ${pn} ${a.offsetSeconds >= 0 ? 'بعد' : 'قبل'} (${Math.abs(a.offsetSeconds/60)}د)`;
          } else {
            info += ` ${String(a.hour).padStart(2, '0')}:${String(a.minute).padStart(2, '0')}`;
          }
          info += ` - صوت: ${a.volume || 20}`;
          if (a.loop > 0) info += ` (تكرار ${a.loop}د)`;
          return `<li class="file-item"><span>${info}</span><button class="btn btn-danger" onclick="deleteEidSchedule(${realIndex})">حذف</button></li>`;
        }).join('')
      : '<p style="text-align:center;opacity:0.7">لا توجد تنبيهات عيد. أضف تنبيهاً أعلاه.</p>';
  });
}

function deleteEidSchedule(index) {
  apiPost('/api/scheduler/delete', { index }).then(() => {
    toast('تم حذف تنبيه العيد');
    loadEidSchedules();
  }).catch((err) => toast(`فشل الحذف: ${err.message}`));
}

function loadEidTakbeerConfig() {
  apiGet('/api/eid/takbeer_config', {}).then((data) => {
    if (!$('eidPrayerConfig')) return;
    $('eidPrayerConfig').innerHTML = prayerNames.map((name, i) => {
      const cfg = data.prayers ? data.prayers[i] : {enabled: i < 5, before: 15, after: 15};
      const enableBefore = cfg.before > 0 ? 'checked' : '';
      const enableAfter = cfg.after > 0 ? 'checked' : '';
      return `<div class="eid-takbeer-row">
        <label class="prayer-label"><input type="checkbox" class="eid-prayer-cb" value="${i}" ${cfg.enabled?'checked':''}> ${name}</label>
        <label><input type="checkbox" class="eid-prayer-cb-before" ${enableBefore}> قبل <input type="number" class="eid-prayer-before" value="${cfg.before>0?cfg.before:15}" min="0" style="width:50px;margin:0 5px"> د</label>
        <label><input type="checkbox" class="eid-prayer-cb-after" ${enableAfter}> بعد <input type="number" class="eid-prayer-after" value="${cfg.after>0?cfg.after:15}" min="0" style="width:50px;margin:0 5px"> د</label>
      </div>`;
    }).join('');
  });
}

function saveEidTakbeerConfig() {
  const prayers = [];
  document.querySelectorAll('.eid-prayer-cb').forEach((cb, i) => {
    const cbBefore = document.querySelectorAll('.eid-prayer-cb-before')[i];
    const beforeInput = document.querySelectorAll('.eid-prayer-before')[i];
    const cbAfter = document.querySelectorAll('.eid-prayer-cb-after')[i];
    const afterInput = document.querySelectorAll('.eid-prayer-after')[i];
    prayers.push({
      enabled: cb.checked,
      before: cbBefore?.checked ? parseInt(beforeInput?.value || '0') : 0,
      after: cbAfter?.checked ? parseInt(afterInput?.value || '0') : 0
    });
  });
  apiPost('/api/eid/takbeer_config/save', {json: JSON.stringify(prayers)})
    .then(() => toast('تم حفظ جدولة التكبيرات'))
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
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

function loadSessionTimeout() {
  apiGet('/api/session/timeout', {}).then((data) => {
    const mins = data.timeout || 10;
    if ($('sessionTimeout')) $('sessionTimeout').value = mins;
    localStorage.setItem('vivoSessionTimeout', String(mins));
  });
}

function saveSessionTimeout() {
  const mins = parseInt($('sessionTimeout')?.value || '10');
  if (mins < 1) return toast('المدة يجب أن تكون دقيقة واحدة على الأقل');
  apiPost('/api/session/timeout/save', { timeout: mins })
    .then(() => {
      localStorage.setItem('vivoSessionTimeout', String(mins));
      toast(`تم حفظ مدة الجلسة: ${mins} دقائق`);
    })
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadManualTimeStatus() {
  apiGet('/api/time/manual_status', {}).then((data) => {
    if ($('manualTimeToggle')) $('manualTimeToggle').checked = !!data.enabled;
    if ($('manualYear')) $('manualYear').value = data.year || 2026;
    if ($('manualMonth')) $('manualMonth').value = data.month || 1;
    if ($('manualDay')) $('manualDay').value = data.day || 1;
    
    let h = data.hour || 0;
    let ampm = 'am';
    if (h >= 12) {
      ampm = 'pm';
      if (h > 12) h -= 12;
    } else if (h === 0) {
      h = 12;
    }
    if ($('manualHour')) $('manualHour').value = h;
    if ($('manualAmPm')) $('manualAmPm').value = ampm;
    
    if ($('manualMinute')) $('manualMinute').value = data.minute || 0;
    if ($('manualTimeFields')) $('manualTimeFields').style.display = data.enabled ? 'block' : 'none';
  });
}

function saveManualTime() {
  const enabled = $('manualTimeToggle')?.checked || false;
  
  let hour = parseInt($('manualHour')?.value || '12');
  const ampm = $('manualAmPm')?.value || 'am';
  if (ampm === 'pm' && hour < 12) hour += 12;
  if (ampm === 'am' && hour === 12) hour = 0;
  
  apiPost('/api/time/manual_save', {
    enabled: enabled ? '1' : '0',
    year: $('manualYear')?.value || '2026',
    month: $('manualMonth')?.value || '1',
    day: $('manualDay')?.value || '1',
    hour: hour.toString(),
    minute: $('manualMinute')?.value || '0'
  }).then(() => {
    toast('تم حفظ الوقت والتاريخ');
    if (enabled) toast('تم تطبيق الوقت اليدوي الآن');
    updateClock();
    fetchPrayerTimes();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function toggleManualTime() {
  if ($('manualTimeFields')) $('manualTimeFields').style.display = $('manualTimeToggle')?.checked ? 'block' : 'none';
}

function checkSessionTimeout() {
  const sessionTime = parseInt(localStorage.getItem('vivoSessionTime') || '0');
  const timeoutMinutes = parseInt(localStorage.getItem('vivoSessionTimeout') || '10');
  if (sessionTime > 0 && (Date.now() - sessionTime) > (timeoutMinutes * 60000)) {
    doLogout();
  }
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
  toggleEidScheduleFields();
  toggleEidLoopFields();
  populateGpioPins();
  if ($('scheduleVolume')) {
    $('scheduleVolume').addEventListener('input', () => {
      if ($('scheduleVolumeValue')) $('scheduleVolumeValue').textContent = $('scheduleVolume').value;
    });
  }
  const sessionTime = parseInt(localStorage.getItem('vivoSessionTime') || '0');
  let timeoutMinutes = parseInt(localStorage.getItem('vivoSessionTimeout') || '10');
  const sessionValid = (Date.now() - sessionTime) < (timeoutMinutes * 60000);

  if (sessionValid && appState.password) {
      $('loginPassword').value = appState.password;
      doLogin();
  }

  if ($('eidScheduleVolume')) {
    $('eidScheduleVolume').addEventListener('input', () => {
      if ($('eidScheduleVolumeValue')) $('eidScheduleVolumeValue').textContent = $('eidScheduleVolume').value;
    });
  }

  let prayerFetchCounter = 0;
  setInterval(() => {
    if ($('mainContent')?.style.display !== 'none') {
      updateClock();
      fetchStatus();
      checkSessionTimeout();
      prayerFetchCounter++;
      if (prayerFetchCounter % 6 === 0) fetchPrayerTimes();
    }
  }, 10000);
});

function togglePasswordVisibility(inputId, iconId) {
  const input = $(inputId);
  const icon = $(iconId);
  if (!input || !icon) return;
  if (input.type === 'password') {
    input.type = 'text';
    icon.classList.remove('fa-eye');
    icon.classList.add('fa-eye-slash');
  } else {
    input.type = 'password';
    icon.classList.remove('fa-eye-slash');
    icon.classList.add('fa-eye');
  }
}
