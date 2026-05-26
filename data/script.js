const appState = {
  files: [],
  countries: [],
  cities: [],
  playlist: [],
  sessionToken: localStorage.getItem('vivoSessionToken') || '',
  activeTab: 'dashboard',
  prayerUiLoaded: false,
  csvStatusLoaded: false
};

const $ = (id) => document.getElementById(id);

let currentDir = '/';
let calendarEditorYear = 0;
let calendarEditorMonth = 0;

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
  const payload = { ...data };
  if (appState.sessionToken && !payload.token) payload.token = appState.sessionToken;
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: formBody(payload)
  }).then((response) => {
    if (response.status === 401) {
      doLogout();
      throw new Error(`unauthorized ${url}`);
    }
    if (!response.ok) throw new Error(`${response.status} ${url}`);
    return response.json().catch(() => ({}));
  });
}

function showTab(tabName) {
  const previousTab = appState.activeTab;
  appState.activeTab = tabName;
  document.querySelectorAll('.tab').forEach((tab) => tab.classList.remove('active'));
  document.querySelectorAll('.sidebar a').forEach((link) => link.classList.remove('active'));

  const tab = $(`tab-${tabName}`);
  if (tab) tab.classList.add('active');

  const nav = document.querySelector(`.sidebar a[href="#${tabName}"]`);
  if (nav) nav.classList.add('active');

  // Close sidebar on mobile after selecting a tab
  if (document.body.classList.contains('is-mobile')) {
    const sidebar = document.getElementById('sidebar');
    const overlay = document.getElementById('sidebarOverlay');
    const menuBtnIcon = document.querySelector('#menuToggleBtn i');
    if (sidebar && sidebar.classList.contains('open')) {
      sidebar.classList.remove('open');
      if (overlay) overlay.style.display = 'none';
      if (menuBtnIcon) {
        menuBtnIcon.classList.remove('fa-times');
        menuBtnIcon.classList.add('fa-bars');
      }
    }
  }

  if (tabName === 'files') { loadFileList(); }
  if (tabName === 'scheduler') { loadSchedules(); }
  if (tabName === 'gpio') { loadGpioMappings(); loadGpioSchedules(); }
  if (tabName === 'maghrib') { loadMaghribAlerts(); }
  if (tabName === 'network') { loadWifiStatus(); }
  if (tabName === 'prayer') { loadPrayerTab(previousTab !== 'prayer'); }
  if (tabName === 'settings') { loadStartupSettings(); loadSessionTimeout(); loadManualTimeStatus(); }
  if (tabName === 'eid') { loadEidSchedules(); loadEidTakbeerConfig(); }
  if (tabName === 'logs') { fetchLogs(); }
}

function toggleSidebar() {
  const sidebar = document.getElementById('sidebar');
  const overlay = document.getElementById('sidebarOverlay');
  const menuBtnIcon = document.querySelector('#menuToggleBtn i');
  if (!sidebar) return;
  
  if (sidebar.classList.contains('open')) {
    sidebar.classList.remove('open');
    if (overlay) overlay.style.display = 'none';
    if (menuBtnIcon) {
      menuBtnIcon.classList.remove('fa-times');
      menuBtnIcon.classList.add('fa-bars');
    }
  } else {
    sidebar.classList.add('open');
    if (overlay) overlay.style.display = 'block';
    if (menuBtnIcon) {
      menuBtnIcon.classList.remove('fa-bars');
      menuBtnIcon.classList.add('fa-times');
    }
  }
}

function doLogin() {
  const password = $('loginPassword')?.value || '';
  apiPost('/api/password/check', { password })
    .then((data) => {
      if (data.ok) {
        document.body.classList.add('logged-in');
        const overlay = $('loginOverlay');
        if (overlay) overlay.style.display = 'none';
        const main = $('mainContent');
        if (main) main.style.display = 'block';
        const err = $('loginError');
        if (err) err.style.display = 'none';
        if (data.token) {
          appState.sessionToken = data.token;
          localStorage.setItem('vivoSessionToken', data.token);
        }
        localStorage.setItem('vivoSessionTime', Date.now().toString());
        initDashboard();
      } else {
        const err = $('loginError');
        if (err) err.style.display = 'block';
      }
    })
    .catch(() => {
      const err = $('loginError');
      if (err) err.style.display = 'block';
    });
}

function doLogout() {
    localStorage.removeItem('vivoSessionTime');
    localStorage.removeItem('vivoSessionToken');
    appState.sessionToken = '';
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
                appState.sessionToken = '';
                localStorage.removeItem('vivoSessionToken');
                toast('تم إعادة تعيين كلمة المرور بنجاح، يرجى تسجيل الدخول مرة أخرى');
            } else {
                toast('كود الاستعادة غير صحيح');
            }
        })
        .catch((err) => toast(`فشل: ${err.message}`));
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
    setTimeout(fetchPrayerTimes, 700);
  });

  const startupJobs = [
    fetchStatus,
    loadCountries,
    loadManualSettings,
    loadDailyOffsetStatus,
    loadSchedules,
    loadMaghribAlerts,
    loadStartupSettings,
    loadSessionTimeout,
    loadWifiStatus,
    loadEidModeStatus,
    loadEidSchedules,
    loadEidTakbeerConfig,
    populateGpioPins,
    loadDDNS
  ];
  startupJobs.forEach((job, index) => setTimeout(job, 150 + (index * 180)));
  if (appState.activeTab === 'prayer') setTimeout(loadCsvStatus, 3000);
  setTimeout(loadFileList, 3000);
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
    const isAdhan = data.adhan === true;
    const statusCard = $('playbackStatusCard');
    const statusIcon = $('playbackStatusIcon');
    if (statusCard) {
      statusCard.classList.toggle('is-playing', !!data.playing && !isAdhan);
      statusCard.classList.toggle('is-adhan', isAdhan);
    }
    if (statusIcon) {
      if (isAdhan) statusIcon.innerHTML = '<i class="fas fa-mosque"></i>';
      else if (data.playing) statusIcon.innerHTML = '<i class="fas fa-play"></i>';
      else statusIcon.innerHTML = '<i class="fas fa-stop"></i>';
    }
    if ($('playingStatus')) {
      $('playingStatus').textContent = data.playing ? (data.status_text || `يعمل: ${data.file || ''}`) : 'متوقف';
    }
    if ($('volumeSlider') && data.volume !== undefined) {
      $('volumeSlider').value = data.volume;
      if ($('mainVolVal')) $('mainVolVal').textContent = data.volume;
    }
    updateEidModeBanner(!!data.eidMode);
    if ($('playlistVolume') && data.volume !== undefined) {
      $('playlistVolume').value = data.volume;
      if ($('playlistVolVal')) $('playlistVolVal').textContent = data.volume;
    }
    const showControls = data.playing && !isAdhan;
    if ($('stopBtn')) $('stopBtn').style.display = showControls ? 'inline-block' : 'none';
    if ($('dashStopBtn')) $('dashStopBtn').style.display = showControls ? 'inline-block' : 'none';
    if ($('playerControlsContainer')) {
      $('playerControlsContainer').style.display = data.playing ? 'block' : 'none';
    }
    if ($('dashPlayerControlsContainer')) {
      $('dashPlayerControlsContainer').style.display = data.playing ? 'block' : 'none';
    }
    if ($('playerControls')) {
      $('playerControls').style.display = data.playing && !isAdhan ? 'block' : 'none';
    }
    if ($('dashPlayerControls')) {
      $('dashPlayerControls').style.display = data.playing && !isAdhan ? 'block' : 'none';
    }
    if ($('playPauseBtn')) {
      if (data.state === 1) $('playPauseBtn').innerHTML = '<i class="fas fa-pause"></i> إيقاف مؤقت';
      else if (data.state === 2) $('playPauseBtn').innerHTML = '<i class="fas fa-play"></i> تشغيل';
    }
    if ($('dashPlayPauseBtn')) {
      if (data.state === 1) $('dashPlayPauseBtn').innerHTML = '<i class="fas fa-pause"></i> إيقاف مؤقت';
      else if (data.state === 2) $('dashPlayPauseBtn').innerHTML = '<i class="fas fa-play"></i> تشغيل';
    }
  });
}

function fetchTrackInfo() {
  apiGet('/api/audio/track_info', {}).then((data) => {
    const durationStr = data.duration ? formatTime(data.duration) : '--:--';
    const positionStr = data.position !== undefined ? formatTime(data.position) : '00:00';
    const pct = (data.duration > 0 && data.position !== undefined) ? Math.min(100, (data.position / data.duration) * 100) : 0;
    
    if ($('trackDuration')) $('trackDuration').textContent = durationStr;
    if ($('trackPosition')) $('trackPosition').textContent = positionStr;
    if ($('trackProgress')) $('trackProgress').value = pct;

    if ($('dashTrackDuration')) $('dashTrackDuration').textContent = durationStr;
    if ($('dashTrackPosition')) $('dashTrackPosition').textContent = positionStr;
    if ($('dashTrackProgress')) $('dashTrackProgress').value = pct;
  });
}

function formatTime(seconds) {
  if (!seconds && seconds !== 0) return '--:--';
  const m = Math.floor(seconds / 60);
  const s = Math.floor(seconds % 60);
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

function setVolume(value) {
  apiPost('/api/audio/volume', { volume: value })
    .then(fetchStatus)
    .catch((err) => {
      if (err.message && err.message.includes('adhan_playing')) {
        toast('لا يمكن تعديل الصوت أثناء الأذان أو الإقامة');
      } else {
        console.error(err);
      }
    });
}

function stopAudio() {
  apiPost('/api/audio/stop')
    .then(() => {
      fetchStatus();
      if ($('playerControlsContainer')) $('playerControlsContainer').style.display = 'none';
      if ($('dashPlayerControlsContainer')) $('dashPlayerControlsContainer').style.display = 'none';
    })
    .catch((err) => {
      if (err.message && err.message.includes('adhan_playing')) {
        toast('لا يمكن إيقاف التشغيل أثناء الأذان أو الإقامة');
      } else {
        console.error(err);
      }
    });
}

function pauseAudio() {
  apiPost('/api/audio/pause')
    .then(fetchStatus)
    .catch((err) => console.error(err));
}

function resumeAudio() {
  apiPost('/api/audio/resume')
    .then(fetchStatus)
    .catch((err) => console.error(err));
}

function seekAudio(seconds) {
  apiPost('/api/audio/seek', { seconds })
    .catch((err) => console.error(err));
}

function seekForward() {
  apiGet('/api/audio/track_info', {}).then((data) => {
    const pos = data.position || 0;
    seekAudio(pos + 10);
  }).catch(() => {});
}

function seekBackward() {
  apiGet('/api/audio/track_info', {}).then((data) => {
    const pos = data.position || 0;
    seekAudio(Math.max(0, pos - 10));
  }).catch(() => {});
}

function togglePlayPause() {
  apiGet('/api/audio/track_info', {}).then((data) => {
    if (data.adhan) {
      toast('لا يمكن التحكم في التشغيل أثناء الأذان أو الإقامة');
      return;
    }
    if (data.state === 2) { // AUDIO_PAUSED
      resumeAudio();
      if ($('playPauseBtn')) $('playPauseBtn').innerHTML = '<i class="fas fa-pause"></i> إيقاف مؤقت';
      if ($('dashPlayPauseBtn')) $('dashPlayPauseBtn').innerHTML = '<i class="fas fa-pause"></i> إيقاف مؤقت';
    } else if (data.state === 1) { // AUDIO_PLAYING
      pauseAudio();
      if ($('playPauseBtn')) $('playPauseBtn').innerHTML = '<i class="fas fa-play"></i> تشغيل';
      if ($('dashPlayPauseBtn')) $('dashPlayPauseBtn').innerHTML = '<i class="fas fa-play"></i> تشغيل';
    }
  }).catch(() => {});
}

function setTrackProgress(value) {
  if (!value) return;
  apiGet('/api/audio/track_info', {}).then((data) => {
    if (data.duration > 0) {
      const seconds = Math.floor((value / 100) * data.duration);
      seekAudio(seconds);
    }
  }).catch(() => {});
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

function toggleDDNS(userTriggered = false) {
  if ($('ddnsFields')) $('ddnsFields').style.display = $('ddnsToggle')?.checked ? 'block' : 'none';
  if (userTriggered) {
    saveDDNS();
  }
}

function saveDDNS() {
  const enabled = $('ddnsToggle')?.checked ? '1' : '0';
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

function loadPrayerTab(force = false) {
  if (!appState.prayerUiLoaded || force) {
    appState.prayerUiLoaded = true;
    loadCountries();
    loadManualSettings();
    loadDailyOffsetStatus();
  } else {
    fetchPrayerTimes();
  }
  if (!appState.csvStatusLoaded || force) loadCsvStatus();
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
        if ($('egyptDstToggle') && cfg.egyptDst !== undefined) {
          $('egyptDstToggle').checked = cfg.egyptDst;
        }
        const egyptDstCont = $('egyptDstContainer');
        if (egyptDstCont) {
          egyptDstCont.style.display = (cfg.country === 'Egypt' || cfg.country === 'مصر') ? 'flex' : 'none';
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
  const egyptDstCont = $('egyptDstContainer');
  if (egyptDstCont) {
    egyptDstCont.style.display = (country === 'Egypt' || country === 'مصر') ? 'flex' : 'none';
  }
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
  const egyptDst = $('egyptDstToggle')?.checked ? 1 : 0;
  const query = country && city ? 
    `?country=${encodeURIComponent(country)}&city=${encodeURIComponent(city)}&method=${method}&egyptDst=${egyptDst}` : 
    `?method=${method}&egyptDst=${egyptDst}`;

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

function loadDailyOffsetStatus() {
  const dateParam = $('dailyOffsetDate')?.value || '';
  const query = dateParam ? `?date=${encodeURIComponent(dateParam)}` : '';
  apiGet(`/api/prayer/daily_offset${query}`, { ok: false }).then((data) => {
    if (!data.ok) return;
    if ($('dailyOffsetDate')) $('dailyOffsetDate').value = data.date || '';
    if ($('dailyOffsetFajr')) $('dailyOffsetFajr').value = data.fajr || 0;
    if ($('dailyOffsetDhuhr')) $('dailyOffsetDhuhr').value = data.dhuhr || 0;
    if ($('dailyOffsetAsr')) $('dailyOffsetAsr').value = data.asr || 0;
    if ($('dailyOffsetMaghrib')) $('dailyOffsetMaghrib').value = data.maghrib || 0;
    if ($('dailyOffsetIsha')) $('dailyOffsetIsha').value = data.isha || 0;
    if ($('dailyOffsetStatus')) {
      $('dailyOffsetStatus').textContent = data.exists ? `يوجد تصحيح محفوظ لهذا اليوم: ${data.date}` : `لا يوجد تصحيح محفوظ لهذا اليوم: ${data.date}`;
    }
  });
}

function saveDailyOffset() {
  apiPost('/api/prayer/daily_offset', {
    date: $('dailyOffsetDate')?.value || '',
    fajr: $('dailyOffsetFajr')?.value || 0,
    dhuhr: $('dailyOffsetDhuhr')?.value || 0,
    asr: $('dailyOffsetAsr')?.value || 0,
    maghrib: $('dailyOffsetMaghrib')?.value || 0,
    isha: $('dailyOffsetIsha')?.value || 0
  }).then(() => {
    toast('تم حفظ تصحيح هذا اليوم فقط');
    loadDailyOffsetStatus();
    fetchPrayerTimes();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function deleteDailyOffset() {
  apiPost('/api/prayer/daily_offset/delete', {
    date: $('dailyOffsetDate')?.value || ''
  }).then(() => {
    toast('تم حذف تصحيح هذا اليوم');
    loadDailyOffsetStatus();
    fetchPrayerTimes();
  }).catch((err) => toast(`فشل الحذف: ${err.message}`));
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
    loadAdhanSettings();
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
  const audioFiles = appState.files.filter((f) =>
    !f.isDirectory &&
    !String(f.name || '').startsWith('prayer_csv/') &&
    /\.(mp3|wav)$/i.test(f.name || '')
  );
  const options = audioFiles
    .map((f) => `<option value="${safeAttr(f.name)}">${safeText(f.name)}</option>`)
    .join('');
  const optionsWithNone = '<option value="">بدون صوت</option>' + options;

  [
    'fajrAdhanFileSelect', 'adhanFileSelect', 'iqamaFileSelect', 'singleAlertFile',
    'eidTakbeerFile', 'playlistFileSelect', 'startupFileSelect'
  ].forEach((id) => { if ($(id)) $(id).innerHTML = options; });

  [
    'gpioSchedAlertFile', 'gpioInputFile'
  ].forEach((id) => { if ($(id)) $(id).innerHTML = optionsWithNone; });

  if ($('playlistFilesChecklist')) {
    $('playlistFilesChecklist').innerHTML = audioFiles
      .map((f) => `
        <label style="display:flex; align-items:center; gap:8px; margin-bottom:6px; cursor:pointer;">
          <input type="checkbox" class="playlist-file-cb" value="${safeAttr(f.name)}">
          <span>${safeText(f.name)}</span>
        </label>
      `).join('');
  }
  if ($('eidScheduleFilesChecklist')) {
    $('eidScheduleFilesChecklist').innerHTML = audioFiles
      .map((f) => `
        <label style="display:flex; align-items:center; gap:8px; margin-bottom:6px; cursor:pointer;">
          <input type="checkbox" class="eid-file-cb" value="${safeAttr(f.name)}">
          <span>${safeText(f.name)}</span>
        </label>
      `).join('');
  }
  loadEidModeStatus();
}

function uploadFile() {
  const input = $('fileInput');
  if (!input?.files?.length) return toast('اختر ملفاً أولاً');
  
  const file = input.files[0];
  const folder = $('uploadFolderSelect')?.value || '/';

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

  const chunkSize = 64 * 1024;
  let offset = 0;
  const startTime = Date.now();
  let retryCount = 0;
  const maxRetries = 3;

  const sendChunk = () => {
      const end = Math.min(offset + chunkSize, file.size);
      const chunk = file.slice(offset, end);
      const finalChunk = end >= file.size ? '1' : '0';
      const url = `/api/files/upload_chunk?name=${encodeURIComponent(file.name)}&folder=${encodeURIComponent(folder)}&offset=${offset}&total=${file.size}&final=${finalChunk}`;
      const xhr = new XMLHttpRequest();
      xhr.open('POST', url, true);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');

      xhr.upload.onprogress = function(e) {
          const loaded = offset + (e.lengthComputable ? e.loaded : 0);
          const percentComplete = Math.min(100, Math.round((loaded / file.size) * 100));
          if (progressBar) progressBar.value = percentComplete;
          
          const elapsed = (Date.now() - startTime) / 1000;
          let speedText = '';
          if (elapsed > 0.5) {
              const speedBytesPerSec = loaded / elapsed;
              if (speedBytesPerSec > 1024 * 1024) {
                  speedText = ` (${(speedBytesPerSec / (1024 * 1024)).toFixed(1)} MB/s)`;
              } else {
                  speedText = ` (${(speedBytesPerSec / 1024).toFixed(1)} KB/s)`;
              }
          }
          if (statusDiv) {
              statusDiv.style.color = '';
              statusDiv.textContent = `جاري الرفع... ${percentComplete}%${speedText}`;
          }
      };

      const handleFailure = (errMessage) => {
          if (retryCount < maxRetries) {
              retryCount++;
              if (statusDiv) {
                  statusDiv.style.color = '#f39c12';
                  statusDiv.textContent = `فشل مؤقت، إعادة المحاولة ${retryCount}/${maxRetries}...`;
              }
              setTimeout(sendChunk, 1500);
          } else {
              if (statusDiv) {
                  statusDiv.style.color = '#e74c3c';
                  statusDiv.textContent = errMessage;
              }
              if (progressBar) progressBar.style.display = 'none';
          }
      };

      xhr.onload = function() {
          if (xhr.status !== 200) {
              handleFailure('فشل الرفع. خطأ من السيرفر: ' + xhr.status);
              return;
          }

          retryCount = 0; // Reset retry count on success
          offset = end;
          const percentComplete = Math.min(100, Math.round((offset / file.size) * 100));
          if (progressBar) progressBar.value = percentComplete;

          if (offset < file.size) {
              sendChunk();
          } else {
              if (statusDiv) {
                  statusDiv.style.color = '#2ecc71';
                  statusDiv.textContent = `تم الرفع كاملاً (${(file.size / (1024 * 1024)).toFixed(2)} MB)`;
              }
              input.value = '';
              loadFileList();
              setTimeout(() => { if (progressBar) progressBar.style.display = 'none'; }, 5000);
          }
      };

      xhr.onerror = function() {
          handleFailure('حدث خطأ في الاتصال أثناء الرفع.');
      };

      xhr.send(chunk);
  };

  sendChunk();
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
  const payload = {
    fajr: $('fajrAdhanFileSelect')?.value || '',
    adhan: $('adhanFileSelect')?.value || '',
    iqama: $('iqamaFileSelect')?.value || ''
  };
  for (let i = 0; i < 5; i++) {
    payload[`iqama_en_${i}`] = !!$(`iqama_en_${i}`)?.checked;
    payload[`iqama_del_${i}`] = $(`iqama_del_${i}`)?.value || '10';
  }
  apiPost('/api/adhan/files', payload)
    .then(() => toast('تم حفظ ملفات الأذان وتفضيلات الإقامة'))
    .catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function loadAdhanSettings() {
  apiGet('/api/adhan/files', {}).then((data) => {
    if (data.fajr && $('fajrAdhanFileSelect')) $('fajrAdhanFileSelect').value = data.fajr;
    if (data.adhan && $('adhanFileSelect')) $('adhanFileSelect').value = data.adhan;
    if (data.iqama && $('iqamaFileSelect')) $('iqamaFileSelect').value = data.iqama;
    for (let i = 0; i < 5; i++) {
      const enCb = $(`iqama_en_${i}`);
      const delInput = $(`iqama_del_${i}`);
      if (enCb) enCb.checked = !!data[`iqama_en_${i}`];
      if (delInput) delInput.value = data[`iqama_del_${i}`] !== undefined ? data[`iqama_del_${i}`] : 10;
    }
  }).catch((err) => console.error('Failed to load adhan settings:', err));
}

let editingSingleAlertIndex = -1;
let editingPlaylistSchedIndex = -1;
let editingGpioSchedIndex = -1;
let editingInputPin = null;
let editingInputOutputPin = null;

function toggleScheduleFields(prefix = 'singleAlert') {
  const type = $(`${prefix}Type`)?.value || 'daily';
  let html = '';
  if (type === 'weekly') {
    html = '<label>اختر الأيام</label><div id="' + prefix + 'WeeklyDays">' +
      dayNames.map((d, i) => `<label style="display:inline-flex;align-items:center;gap:4px;margin:4px 8px 4px 0;font-size:var(--fs-small)"><input type="checkbox" class="${prefix}-weekly-day-cb" value="${i}"> ${d}</label>`
    ).join('') + '</div>';
  } else if (type === 'monthly') {
    html = '<label>اليوم من الشهر</label><input type="number" id="' + prefix + 'Day" min="1" max="31" value="1">';
  } else if (type === 'specific') {
    html = '<label>التاريخ</label><input type="date" id="' + prefix + 'Date">';
  } else if (type === 'yearly') {
    html = '<label>التاريخ السنوي (السنة غير مهمة)</label><input type="date" id="' + prefix + 'Date">';
  } else if (type === 'prayer_relative') {
    html = '<label>الصلاة</label><select id="' + prefix + 'Prayer"><option value="0">الفجر</option><option value="1">الظهر</option><option value="2">العصر</option><option value="3">المغرب</option><option value="4">العشاء</option></select>' +
      '<label>الإزاحة بالدقائق (سالب أو موجب)</label><input type="number" id="' + prefix + 'Offset" value="0">' +
      '<label>أيام التشغيل (اختياري - اتركه فارغاً للتشغيل يومياً)</label><div id="' + prefix + 'WeeklyDays">' +
      dayNames.map((d, i) => `<label style="display:inline-flex;align-items:center;gap:4px;margin:4px 8px 4px 0;font-size:var(--fs-small)"><input type="checkbox" class="${prefix}-weekly-day-cb" value="${i}"> ${d}</label>`
    ).join('') + '</div>';
  }
  const extraFields = $(`${prefix}ExtraFields`);
  if (extraFields) extraFields.innerHTML = html;

  const timeContainer = $(`${prefix}TimeContainer`);
  if (timeContainer) {
    timeContainer.style.display = type === 'prayer_relative' ? 'none' : 'block';
  }
}

function toggleLoopFields(prefix = 'singleAlert') {
  const loopFields = $(`${prefix}LoopFields`);
  const loopToggle = $(`${prefix}LoopToggle`);
  if (loopFields && loopToggle) {
    loopFields.style.display = loopToggle.value === 'yes' ? 'block' : 'none';
  }
}

function toggleAlertEndTimeField(prefix) {
  const interval = Number($(`${prefix}RepeatInterval`)?.value || 0);
  const container = $(`${prefix}EndTimeContainer`);
  if (container) {
    container.style.display = interval > 0 ? 'block' : 'none';
  }
}

function toggleAlertGpioFields(prefix) {
  const active = $(`${prefix}GpioActive`)?.checked;
  const fields = $(`${prefix}GpioFields`);
  if (fields) {
    fields.style.display = active ? 'block' : 'none';
  }
}

function toggleAlertGpioDurationFields(prefix) {
  const mode = $(`${prefix}GpioDurationMode`)?.value;
  const container = $(`${prefix}GpioDurationContainer`);
  if (container) {
    container.style.display = mode === 'custom' ? 'block' : 'none';
  }
}

function toggleGpioAudioModeFields(prefix) {
  const mode = $(`${prefix}AudioMode`)?.value;
  const container = $(`${prefix}RepeatCountContainer`);
  if (container) {
    container.style.display = mode === 'repeat' ? 'block' : 'none';
  }
}

function toggleGpioFileSelected(prefix) {
  const fileSelect = (prefix === 'gpioInput') ? $('gpioInputFile') : $('gpioSchedAlertFile');
  const fileVal = fileSelect?.value || '';
  const fields = $(`${prefix}AudioFields`);
  if (fields) {
    fields.style.display = fileVal ? 'block' : 'none';
  }
}

function getSelectedDaysBitmask(prefix) {
  const cbs = document.querySelectorAll(`.${prefix}-weekly-day-cb:checked`);
  if (cbs.length === 0) return -1;
  let mask = 0;
  cbs.forEach(cb => { mask |= (1 << parseInt(cb.value)); });
  // Add 128 to represent it as a bitmask to the backend, to distinguish from legacy single day index (0-6)
  return 128 | mask;
}

function formatDays(mask) {
  if (mask < 0) return '';
  if (mask >= 128) {
    const actualMask = mask & 0x7F;
    const days = [];
    for (let i = 0; i < 7; i++) {
      if (actualMask & (1 << i)) days.push(dayNames[i]);
    }
    return days.join('، ');
  }
  if (mask >= 0 && mask <= 6) return dayNames[mask];
  // In case of non-prefixed bitmask (legacy backup)
  const days = [];
  for (let i = 0; i < 7; i++) {
    if (mask & (1 << i)) days.push(dayNames[i]);
  }
  return days.join('، ');
}

function saveSingleAlert() {
  const name = $('singleAlertName')?.value || '';
  const file = $('singleAlertFile')?.value || '';
  if (!file) return toast('يجب اختيار ملف صوّتي');

  const [hour = '0', minute = '0'] = ($('singleAlertTime')?.value || '00:00').split(':');
  const type = $('singleAlertType')?.value || 'daily';
  let dayOfWeek = -1, dayOfMonth = -1;
  if (type === 'weekly' || type === 'prayer_relative') dayOfWeek = getSelectedDaysBitmask('singleAlert');
  if (type === 'monthly') dayOfMonth = $('singleAlertDay')?.value || '-1';
  
  const data = {
    name,
    file,
    type,
    hour,
    minute,
    dayOfWeek,
    dayOfMonth,
    specificDate: $('singleAlertDate')?.value || '',
    volume: $('singleAlertVolume')?.value || 20,
    loop: $('singleAlertLoopToggle')?.value === 'yes' ? $('singleAlertLoopDuration')?.value || 0 : 0,
    prayerIndex: $('singleAlertPrayer')?.value || 0,
    offsetSeconds: Number($('singleAlertOffset')?.value || 0) * 60,
    eidOnly: '0',
    index: editingSingleAlertIndex,
    repeatInterval: Number($('singleAlertRepeatInterval')?.value || 0),
    endHour: $('singleAlertEndTime')?.value ? Number($('singleAlertEndTime').value.split(':')[0]) : -1,
    endMinute: $('singleAlertEndTime')?.value ? Number($('singleAlertEndTime').value.split(':')[1]) : -1,
    gpioActive: $('singleAlertGpioActive')?.checked ? 1 : 0,
    gpioPin: $('singleAlertGpioPin')?.value || 0,
    gpioMode: $('singleAlertGpioMode')?.value || 'continuous',
    gpioDurationMode: $('singleAlertGpioDurationMode')?.value || 'audio_duration',
    gpioDurationSec: Number($('singleAlertGpioDurationSec')?.value || 5),
    important: $('singleAlertImportant')?.checked ? 1 : 0
  };

  apiPost('/api/scheduler/add', data).then(() => {
    toast(editingSingleAlertIndex >= 0 ? 'تم تعديل التنبيه بنجاح' : 'تم إضافة التنبيه بنجاح');
    cancelEditSingleAlert();
    loadSchedules();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function cancelEditSingleAlert() {
  editingSingleAlertIndex = -1;
  if ($('singleAlertName')) $('singleAlertName').value = '';
  if ($('singleAlertFile')) $('singleAlertFile').selectedIndex = 0;
  if ($('singleAlertType')) {
    $('singleAlertType').value = 'daily';
    toggleScheduleFields('singleAlert');
  }
  if ($('singleAlertTime')) $('singleAlertTime').value = '00:00';
  if ($('singleAlertVolume')) {
    $('singleAlertVolume').value = 20;
    if ($('singleAlertVolumeValue')) $('singleAlertVolumeValue').textContent = '20';
  }
  if ($('singleAlertLoopToggle')) {
    $('singleAlertLoopToggle').value = 'no';
    toggleLoopFields('singleAlert');
  }
  if ($('singleAlertLoopDuration')) $('singleAlertLoopDuration').value = 0;
  if ($('singleAlertRepeatInterval')) $('singleAlertRepeatInterval').value = 0;
  if ($('singleAlertGpioActive')) {
    $('singleAlertGpioActive').checked = false;
    toggleAlertGpioFields('singleAlert');
  }
  if ($('singleAlertGpioPin')) $('singleAlertGpioPin').value = 3;
  if ($('singleAlertGpioMode')) $('singleAlertGpioMode').value = 'continuous';
  if ($('singleAlertGpioDurationMode')) {
    $('singleAlertGpioDurationMode').value = 'audio_duration';
    toggleAlertGpioDurationFields('singleAlert');
  }
  if ($('singleAlertGpioDurationSec')) $('singleAlertGpioDurationSec').value = 5;
  if ($('singleAlertImportant')) $('singleAlertImportant').checked = true;

  if ($('singleAlertSaveBtn')) $('singleAlertSaveBtn').innerHTML = '<i class="fas fa-save"></i> حفظ التنبيه';
  if ($('singleAlertCancelEditBtn')) $('singleAlertCancelEditBtn').style.display = 'none';
}

function editSingleAlert(index) {
  const alert = appState.alerts[index];
  if (!alert) return;
  
  editingSingleAlertIndex = index;
  
  if ($('singleAlertName')) $('singleAlertName').value = alert.name || '';
  if ($('singleAlertFile')) $('singleAlertFile').value = alert.file || '';
  if ($('singleAlertType')) {
    $('singleAlertType').value = alert.type || 'daily';
    toggleScheduleFields('singleAlert');
  }
  
  if ($('singleAlertTime')) {
    $('singleAlertTime').value = `${String(alert.hour).padStart(2, '0')}:${String(alert.minute).padStart(2, '0')}`;
  }
  
  if ($('singleAlertVolume')) {
    $('singleAlertVolume').value = alert.volume !== undefined ? alert.volume : 20;
    if ($('singleAlertVolumeValue')) $('singleAlertVolumeValue').textContent = alert.volume !== undefined ? alert.volume : 20;
  }
  
  if ($('singleAlertLoopToggle')) {
    $('singleAlertLoopToggle').value = (alert.loop && alert.loop > 0) ? 'yes' : 'no';
    toggleLoopFields('singleAlert');
  }
  if ($('singleAlertLoopDuration') && alert.loop) {
    $('singleAlertLoopDuration').value = alert.loop;
  }
  
  if ($('singleAlertRepeatInterval')) {
    $('singleAlertRepeatInterval').value = alert.repeatInterval !== undefined ? alert.repeatInterval : 0;
    toggleAlertEndTimeField('singleAlert');
  }
  if ($('singleAlertEndTime') && alert.endHour !== undefined && alert.endHour >= 0) {
    const eh = String(alert.endHour).padStart(2, '0');
    const em = String(alert.endMinute).padStart(2, '0');
    $('singleAlertEndTime').value = eh + ':' + em;
  } else if ($('singleAlertEndTime')) {
    $('singleAlertEndTime').value = '';
  }
  if ($('singleAlertGpioActive')) {
    $('singleAlertGpioActive').checked = alert.gpioActive === true || alert.gpioActive === 1;
    toggleAlertGpioFields('singleAlert');
  }
  if ($('singleAlertImportant')) {
    $('singleAlertImportant').checked = (alert.important !== false && alert.important !== 0);
  }
  if ($('singleAlertGpioPin') && alert.gpioPin !== undefined) $('singleAlertGpioPin').value = alert.gpioPin;
  if ($('singleAlertGpioMode') && alert.gpioMode !== undefined) $('singleAlertGpioMode').value = alert.gpioMode;
  if ($('singleAlertGpioDurationMode') && alert.gpioDurationMode !== undefined) {
    $('singleAlertGpioDurationMode').value = alert.gpioDurationMode;
    toggleAlertGpioDurationFields('singleAlert');
  }
  if ($('singleAlertGpioDurationSec') && alert.gpioDurationSec !== undefined) $('singleAlertGpioDurationSec').value = alert.gpioDurationSec;
  
  if ((alert.type === 'weekly' || alert.type === 'prayer_relative') && alert.dayOfWeek >= 0) {
    const mask = alert.dayOfWeek;
    document.querySelectorAll('.singleAlert-weekly-day-cb').forEach(cb => {
      const dayVal = parseInt(cb.value);
      if (mask >= 128) {
        cb.checked = !!((mask & 0x7F) & (1 << dayVal));
      } else {
        cb.checked = (dayVal === mask);
      }
    });
  } else if (alert.type === 'monthly' && $('singleAlertDay')) {
    $('singleAlertDay').value = alert.dayOfMonth;
  } else if (alert.type === 'specific' && $('singleAlertDate')) {
    $('singleAlertDate').value = alert.specificDate || '';
  } else if (alert.type === 'prayer_relative') {
    if ($('singleAlertPrayer')) $('singleAlertPrayer').value = alert.prayerIndex !== undefined ? alert.prayerIndex : 0;
    if ($('singleAlertOffset')) $('singleAlertOffset').value = alert.offsetSeconds !== undefined ? Math.round(alert.offsetSeconds / 60) : 0;
  }
  
  if ($('singleAlertSaveBtn')) $('singleAlertSaveBtn').innerHTML = '<i class="fas fa-save"></i> تعديل التنبيه';
  if ($('singleAlertCancelEditBtn')) $('singleAlertCancelEditBtn').style.display = 'block';
}

function savePlaylistSched() {
  const name = $('playlistSchedName')?.value || '';
  const checkboxes = document.querySelectorAll('.playlist-file-cb:checked');
  if (checkboxes.length === 0) {
    return toast('يجب اختيار ملف واحد على الأقل للقائمة');
  }
  const files = Array.from(checkboxes).map(cb => cb.value).join(',');
  
  const [hour = '0', minute = '0'] = ($('playlistSchedTime')?.value || '00:00').split(':');
  const type = $('playlistSchedType')?.value || 'daily';
  let dayOfWeek = -1, dayOfMonth = -1;
  if (type === 'weekly' || type === 'prayer_relative') dayOfWeek = getSelectedDaysBitmask('playlistSched');
  if (type === 'monthly') dayOfMonth = $('playlistSchedDay')?.value || '-1';
  
  const data = {
    name,
    file: files,
    type,
    hour,
    minute,
    dayOfWeek,
    dayOfMonth,
    specificDate: $('playlistSchedDate')?.value || '',
    volume: $('playlistSchedVolume')?.value || 20,
    loop: $('playlistSchedLoopToggle')?.value === 'yes' ? $('playlistSchedLoopDuration')?.value || 0 : 0,
    prayerIndex: $('playlistSchedPrayer')?.value || 0,
    offsetSeconds: Number($('playlistSchedOffset')?.value || 0) * 60,
    eidOnly: '0',
    index: editingPlaylistSchedIndex,
    repeatInterval: Number($('playlistSchedRepeatInterval')?.value || 0),
    endHour: $('playlistSchedEndTime')?.value ? Number($('playlistSchedEndTime').value.split(':')[0]) : -1,
    endMinute: $('playlistSchedEndTime')?.value ? Number($('playlistSchedEndTime').value.split(':')[1]) : -1,
    gpioActive: $('playlistSchedGpioActive')?.checked ? 1 : 0,
    gpioPin: $('playlistSchedGpioPin')?.value || 0,
    gpioMode: $('playlistSchedGpioMode')?.value || 'continuous',
    gpioDurationMode: $('playlistSchedGpioDurationMode')?.value || 'audio_duration',
    gpioDurationSec: Number($('playlistSchedGpioDurationSec')?.value || 5),
    important: $('playlistSchedImportant')?.checked ? 1 : 0
  };

  apiPost('/api/scheduler/add', data).then(() => {
    toast(editingPlaylistSchedIndex >= 0 ? 'تم تعديل القائمة المجدولة بنجاح' : 'تم إضافة القائمة للجدولة بنجاح');
    cancelEditPlaylistSched();
    loadSchedules();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function cancelEditPlaylistSched() {
  editingPlaylistSchedIndex = -1;
  if ($('playlistSchedName')) $('playlistSchedName').value = '';
  document.querySelectorAll('.playlist-file-cb').forEach(cb => cb.checked = false);
  if ($('playlistSchedType')) {
    $('playlistSchedType').value = 'daily';
    toggleScheduleFields('playlistSched');
  }
  if ($('playlistSchedTime')) $('playlistSchedTime').value = '00:00';
  if ($('playlistSchedVolume')) {
    $('playlistSchedVolume').value = 20;
    if ($('playlistSchedVolumeValue')) $('playlistSchedVolumeValue').textContent = '20';
  }
  if ($('playlistSchedLoopToggle')) {
    $('playlistSchedLoopToggle').value = 'no';
    toggleLoopFields('playlistSched');
  }
  if ($('playlistSchedLoopDuration')) $('playlistSchedLoopDuration').value = 0;
  if ($('playlistSchedRepeatInterval')) $('playlistSchedRepeatInterval').value = 0;
  if ($('playlistSchedGpioActive')) {
    $('playlistSchedGpioActive').checked = false;
    toggleAlertGpioFields('playlistSched');
  }
  if ($('playlistSchedGpioPin')) $('playlistSchedGpioPin').value = 3;
  if ($('playlistSchedGpioMode')) $('playlistSchedGpioMode').value = 'continuous';
  if ($('playlistSchedGpioDurationMode')) {
    $('playlistSchedGpioDurationMode').value = 'audio_duration';
    toggleAlertGpioDurationFields('playlistSched');
  }
  if ($('playlistSchedGpioDurationSec')) $('playlistSchedGpioDurationSec').value = 5;
  if ($('playlistSchedImportant')) $('playlistSchedImportant').checked = true;

  if ($('playlistSchedSaveBtn')) $('playlistSchedSaveBtn').innerHTML = '<i class="fas fa-plus-circle"></i> إضافة القائمة للجدولة';
  if ($('playlistSchedCancelEditBtn')) $('playlistSchedCancelEditBtn').style.display = 'none';
}

function editPlaylistSched(index) {
  const alert = appState.alerts[index];
  if (!alert) return;
  
  editingPlaylistSchedIndex = index;
  
  if ($('playlistSchedName')) $('playlistSchedName').value = alert.name || '';
  
  document.querySelectorAll('.playlist-file-cb').forEach(cb => cb.checked = false);
  
  const filesList = (alert.file || '').split(',');
  filesList.forEach(file => {
    const cb = document.querySelector(`.playlist-file-cb[value="${CSS.escape(file)}"]`);
    if (cb) cb.checked = true;
  });
  
  if ($('playlistSchedType')) {
    $('playlistSchedType').value = alert.type || 'daily';
    toggleScheduleFields('playlistSched');
  }
  
  if ($('playlistSchedTime')) {
    $('playlistSchedTime').value = `${String(alert.hour).padStart(2, '0')}:${String(alert.minute).padStart(2, '0')}`;
  }
  
  if ($('playlistSchedVolume')) {
    $('playlistSchedVolume').value = alert.volume !== undefined ? alert.volume : 20;
    if ($('playlistSchedVolumeValue')) $('playlistSchedVolumeValue').textContent = alert.volume !== undefined ? alert.volume : 20;
  }
  
  if ($('playlistSchedLoopToggle')) {
    $('playlistSchedLoopToggle').value = (alert.loop && alert.loop > 0) ? 'yes' : 'no';
    toggleLoopFields('playlistSched');
  }
  if ($('playlistSchedLoopDuration') && alert.loop) {
    $('playlistSchedLoopDuration').value = alert.loop;
  }
  
  if ($('playlistSchedRepeatInterval')) {
    $('playlistSchedRepeatInterval').value = alert.repeatInterval !== undefined ? alert.repeatInterval : 0;
    toggleAlertEndTimeField('playlistSched');
  }
  if ($('playlistSchedEndTime') && alert.endHour !== undefined && alert.endHour >= 0) {
    const eh = String(alert.endHour).padStart(2, '0');
    const em = String(alert.endMinute).padStart(2, '0');
    $('playlistSchedEndTime').value = eh + ':' + em;
  } else if ($('playlistSchedEndTime')) {
    $('playlistSchedEndTime').value = '';
  }
  if ($('playlistSchedGpioActive')) {
    $('playlistSchedGpioActive').checked = alert.gpioActive === true || alert.gpioActive === 1;
    toggleAlertGpioFields('playlistSched');
  }
  if ($('playlistSchedImportant')) {
    $('playlistSchedImportant').checked = (alert.important !== false && alert.important !== 0);
  }
  if ($('playlistSchedGpioPin') && alert.gpioPin !== undefined) $('playlistSchedGpioPin').value = alert.gpioPin;
  if ($('playlistSchedGpioMode') && alert.gpioMode !== undefined) $('playlistSchedGpioMode').value = alert.gpioMode;
  if ($('playlistSchedGpioDurationMode') && alert.gpioDurationMode !== undefined) {
    $('playlistSchedGpioDurationMode').value = alert.gpioDurationMode;
    toggleAlertGpioDurationFields('playlistSched');
  }
  if ($('playlistSchedGpioDurationSec') && alert.gpioDurationSec !== undefined) $('playlistSchedGpioDurationSec').value = alert.gpioDurationSec;
  
  if ((alert.type === 'weekly' || alert.type === 'prayer_relative') && alert.dayOfWeek >= 0) {
    const mask = alert.dayOfWeek;
    document.querySelectorAll('.playlistSched-weekly-day-cb').forEach(cb => {
      const dayVal = parseInt(cb.value);
      if (mask >= 128) {
        cb.checked = !!((mask & 0x7F) & (1 << dayVal));
      } else {
        cb.checked = (dayVal === mask);
      }
    });
  } else if (alert.type === 'monthly' && $('playlistSchedDay')) {
    $('playlistSchedDay').value = alert.dayOfMonth;
  } else if (alert.type === 'specific' && $('playlistSchedDate')) {
    $('playlistSchedDate').value = alert.specificDate || '';
  } else if (alert.type === 'prayer_relative') {
    if ($('playlistSchedPrayer')) $('playlistSchedPrayer').value = alert.prayerIndex !== undefined ? alert.prayerIndex : 0;
    if ($('playlistSchedOffset')) $('playlistSchedOffset').value = alert.offsetSeconds !== undefined ? Math.round(alert.offsetSeconds / 60) : 0;
  }
  
  if ($('playlistSchedSaveBtn')) $('playlistSchedSaveBtn').innerHTML = '<i class="fas fa-save"></i> تعديل القائمة للجدولة';
  if ($('playlistSchedCancelEditBtn')) $('playlistSchedCancelEditBtn').style.display = 'block';
}

function loadSchedules() {
  apiGet('/api/scheduler/list', []).then((data) => {
    const alerts = Array.isArray(data) ? data : (data.alerts || []);
    appState.alerts = alerts;
    
    const singleAlerts = [];
    const playlistSchedules = [];
    
    alerts.forEach((alert, index) => {
      alert.originalIndex = index;
      if (alert.file && alert.file.includes(',')) {
        playlistSchedules.push(alert);
      } else {
        singleAlerts.push(alert);
      }
    });

    if ($('singleAlertsList')) {
      $('singleAlertsList').innerHTML = singleAlerts.map((a) => {
        let info = `<strong>${safeText(a.name || 'بدون اسم')}</strong>`;
        info += `<br><span style="font-size:0.9em;opacity:0.8;">📄 ${safeText(a.file)}</span>`;
        info += `<br><span style="font-size:0.9em;opacity:0.8;">🕒 ${safeText(a.type)}`;
        if (a.type === 'weekly' && a.dayOfWeek !== undefined) info += ` (${formatDays(a.dayOfWeek)})`;
        if (a.type === 'monthly') info += ` (يوم ${a.dayOfMonth})`;
        if (a.type === 'specific') info += ` (${safeText(a.specificDate)})`;
        if (a.type === 'prayer_relative') {
          const prayerNames = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'];
          info += ` (صلاة ${prayerNames[a.prayerIndex] || a.prayerIndex}، إزاحة ${Math.round(a.offsetSeconds / 60)} دقيقة`;
          if (a.dayOfWeek !== undefined && a.dayOfWeek >= 0) {
            info += `، أيام: ${formatDays(a.dayOfWeek)}`;
          }
          info += `)`;
        }
        if (a.type !== 'prayer_relative' || a.repeatInterval <= 0) {
          info += ` الساعة ${String(a.hour).padStart(2, '0')}:${String(a.minute).padStart(2, '0')}`;
        }
        info += ` | 🔊 ${a.volume}`;
        if (a.loop && a.loop > 0) info += ` (تكرار ${a.loop} ق)`;
        if (a.repeatInterval && a.repeatInterval > 0) {
          info += ` | 🔁 كل ${a.repeatInterval} دقيقة`;
          if (a.endHour !== undefined && a.endHour >= 0) {
            const eh = String(a.endHour).padStart(2, '0');
            const em = String(a.endMinute).padStart(2, '0');
            info += ` (حتى ${eh}:${em})`;
          }
        }
        const impText = (a.important === false || a.important === 0) ? 'غير مهم' : 'مهم';
        info += ` | ⚠️ ${impText}`;
        if (a.gpioActive) {
          const modeArabic = a.gpioMode === 'flasher' ? 'متقطع فلاشر' : a.gpioMode === 'pulse' ? 'نبضة' : 'مستمر';
          const durArabic = a.gpioDurationMode === 'custom' ? `لوقت ${a.gpioDurationSec}ث` : 'لحين انتهاء الصوت';
          info += ` | 🔌 مخرج ${a.gpioPin} (${modeArabic} - ${durArabic})`;
        }
        info += '</span>';
        
        return `<li class="file-item" style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px; margin-bottom:10px;">
          <span>${info}</span>
          <div style="display:flex; gap:8px;">
            <button class="btn btn-secondary" onclick="editSingleAlert(${a.originalIndex})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-edit"></i> تعديل</button>
            <button class="btn btn-danger" onclick="deleteSchedule(${a.originalIndex})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-trash"></i> حذف</button>
          </div>
        </li>`;
      }).join('');
    }

    if ($('playlistSchedulesList')) {
      $('playlistSchedulesList').innerHTML = playlistSchedules.map((a) => {
        let info = `<strong>${safeText(a.name || 'بدون اسم')}</strong>`;
        const count = a.file ? a.file.split(',').length : 0;
        info += `<br><span style="font-size:0.9em;opacity:0.8;">🎵 قائمة تشغيل (${count} ملفات)</span>`;
        info += `<br><span style="font-size:0.9em;opacity:0.8;">🕒 ${safeText(a.type)}`;
        if (a.type === 'weekly' && a.dayOfWeek !== undefined) info += ` (${formatDays(a.dayOfWeek)})`;
        if (a.type === 'monthly') info += ` (يوم ${a.dayOfMonth})`;
        if (a.type === 'specific') info += ` (${safeText(a.specificDate)})`;
        if (a.type === 'prayer_relative') {
          const prayerNames = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'];
          info += ` (صلاة ${prayerNames[a.prayerIndex] || a.prayerIndex}، إزاحة ${Math.round(a.offsetSeconds / 60)} دقيقة`;
          if (a.dayOfWeek !== undefined && a.dayOfWeek >= 0) {
            info += `، أيام: ${formatDays(a.dayOfWeek)}`;
          }
          info += `)`;
        }
        if (a.type !== 'prayer_relative' || a.repeatInterval <= 0) {
          info += ` الساعة ${String(a.hour).padStart(2, '0')}:${String(a.minute).padStart(2, '0')}`;
        }
        info += ` | 🔊 ${a.volume}`;
        if (a.loop && a.loop > 0) info += ` (تكرار ${a.loop} ق)`;
        if (a.repeatInterval && a.repeatInterval > 0) {
          info += ` | 🔁 كل ${a.repeatInterval} دقيقة`;
          if (a.endHour !== undefined && a.endHour >= 0) {
            const eh = String(a.endHour).padStart(2, '0');
            const em = String(a.endMinute).padStart(2, '0');
            info += ` (حتى ${eh}:${em})`;
          }
        }
        const impText = (a.important === false || a.important === 0) ? 'غير مهم' : 'مهم';
        info += ` | ⚠️ ${impText}`;
        if (a.gpioActive) {
          const modeArabic = a.gpioMode === 'flasher' ? 'متقطع فلاشر' : a.gpioMode === 'pulse' ? 'نبضة' : 'مستمر';
          const durArabic = a.gpioDurationMode === 'custom' ? `لوقت ${a.gpioDurationSec}ث` : 'لحين انتهاء الصوت';
          info += ` | 🔌 مخرج ${a.gpioPin} (${modeArabic} - ${durArabic})`;
        }
        info += '</span>';
        
        return `<li class="file-item" style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px; margin-bottom:10px;">
          <span>${info}</span>
          <div style="display:flex; gap:8px;">
            <button class="btn btn-secondary" onclick="editPlaylistSched(${a.originalIndex})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-edit"></i> تعديل</button>
            <button class="btn btn-danger" onclick="deleteSchedule(${a.originalIndex})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-trash"></i> حذف</button>
          </div>
        </li>`;
      }).join('');
    }
  }).catch((err) => console.error('Failed to load schedules:', err));
}

function deleteSchedule(index) {
  if (!confirm('هل أنت متأكد من حذف هذا التنبيه؟')) return;
  apiPost(`/api/scheduler/delete?index=${index}`, { index }).then(() => {
    cancelEditSingleAlert();
    cancelEditPlaylistSched();
    loadSchedules();
  }).catch((err) => toast(`فشل الحذف: ${err.message}`));
}

function populateGpioPins() {
  // Excluded: SD SPI (10-CS,11-MOSI,12-SCK,13-MISO) and I2S (16-BCLK,17-LRCK,18-DOUT)
  const allPins = [3, 4, 5, 6, 7, 8, 9, 14, 15, 19, 47];
  const bestInputs = [8, 9, 14, 47];
  const bestOutputs = [4, 5, 6, 7, 15];

  // Calculate which pins are in use
  const usedInputPins = new Set();
  const usedOutputPins = new Set();

  if (appState.gpioMappings && appState.gpioMappings.inputs) {
    appState.gpioMappings.inputs.forEach(inp => {
      if (editingInputPin === null || Number(inp.pin) !== Number(editingInputPin)) {
        usedInputPins.add(Number(inp.pin));
      }
      if (inp.outputPin && Number(inp.outputPin) !== 0) {
        if (editingInputPin === null || Number(inp.pin) !== Number(editingInputPin)) {
          usedOutputPins.add(Number(inp.outputPin));
        }
      }
    });
  }

  if (appState.gpioMappings && appState.gpioMappings.outputs) {
    appState.gpioMappings.outputs.forEach(out => {
      usedOutputPins.add(Number(out.pin));
    });
  }

  if (appState.gpioSchedules) {
    appState.gpioSchedules.forEach((s, idx) => {
      if (editingGpioSchedIndex === -1 || idx !== editingGpioSchedIndex) {
        usedOutputPins.add(Number(s.pin));
      }
    });
  }

  // Build options for input pins - only bestInputs pins shown
  function buildInputOptions(currentSelectedValue) {
    const available = allPins.filter(p => bestInputs.includes(p) && (!usedInputPins.has(p) || Number(p) === Number(currentSelectedValue)));
    let html = '';
    available.forEach(p => {
      const selected = (Number(p) === Number(currentSelectedValue)) ? ' selected' : '';
      html += `<option value="${p}"${selected}>${p} (أفضل للمدخلات)</option>`;
    });
    if (!html) html = '<option value="">لا توجد دبابيس متاحة</option>';
    return html;
  }

  // Build options for output pins - only bestOutputs pins shown
  function buildOutputOptions(currentSelectedValue, includeNoneOption = false) {
    const available = allPins.filter(p => bestOutputs.includes(p) && (!usedOutputPins.has(p) || Number(p) === Number(currentSelectedValue)));
    let html = includeNoneOption ? '<option value="0">لا يوجد</option>' : '';
    available.forEach(p => {
      const selected = (Number(p) === Number(currentSelectedValue)) ? ' selected' : '';
      html += `<option value="${p}"${selected}>${p} (أفضل للمخرجات)</option>`;
    });
    if (!html && !includeNoneOption) html = '<option value="">لا توجد دبابيس متاحة</option>';
    return html;
  }

  if ($('gpioInputPin')) {
    const currVal = $('gpioInputPin').value;
    $('gpioInputPin').innerHTML = buildInputOptions(currVal);
  }

  if ($('gpioInputOutputPin')) {
    const currVal = $('gpioInputOutputPin').value;
    $('gpioInputOutputPin').innerHTML = buildOutputOptions(currVal, true);
  }

  if ($('outputPin')) {
    const currVal = $('outputPin').value;
    $('outputPin').innerHTML = buildOutputOptions(currVal, false);
  }

  if ($('gpioSchedPin')) {
    const currVal = $('gpioSchedPin').value;
    $('gpioSchedPin').innerHTML = buildOutputOptions(currVal, false);
  }
}

function saveOutputMapping() {
  const pinVal = $('outputPin')?.value;
  if (!pinVal || pinVal === '') return toast('اختر دبوساً صالحاً');
  apiPost('/api/gpio/output', {
    pin: pinVal,
    alert: $('outputAlert')?.value || '',
    duration: $('outputDuration')?.value || 5
  }).then(() => {
    toast('تم حفظ المخرج');
    loadGpioMappings();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function saveSmartInput() {
  const name = $('gpioInputName')?.value || '';
  const pin = $('gpioInputPin')?.value || '0';
  const file = $('gpioInputFile')?.value || '';
  const playDuration = $('gpioInputPlayDuration')?.value || 0;
  const repeatCount = $('gpioInputRepeatCount')?.value || 0;
  const outputPin = $('gpioInputOutputPin')?.value || '0';
  const volume = parseInt($('gpioInputVolume')?.value || '20');
  
  const mode = $('gpioInputOutputMode')?.value || '0';
  let outputDuration = 0;
  if (mode === 'pulse') {
    outputDuration = parseInt($('gpioInputOutputDuration')?.value || '5');
  } else if (mode === '-1') {
    outputDuration = -1;
  }
  
  let deletePromise = Promise.resolve();
  if (editingInputPin !== null && Number(editingInputPin) !== Number(pin)) {
    deletePromise = apiPost(`/api/gpio/input/delete?pin=${editingInputPin}`, { pin: editingInputPin });
  }

  deletePromise.then(() => {
    return apiPost('/api/gpio/input', {
      name,
      pin,
      file,
      playDuration,
      repeatCount,
      outputPin,
      outputDuration,
      volume
    });
  }).then(() => {
    toast('تم حفظ المدخل الذكي');
    cancelEditGpioInput();
    loadGpioMappings();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function editGpioInput(pin) {
  const mapping = appState.gpioMappings?.inputs?.find(inp => inp.pin === pin);
  if (!mapping) return;
  
  editingInputPin = mapping.pin;
  editingInputOutputPin = mapping.outputPin !== undefined ? mapping.outputPin : '0';
  
  populateGpioPins();
  
  if ($('gpioInputName')) $('gpioInputName').value = mapping.name || '';
  if ($('gpioInputPin')) $('gpioInputPin').value = mapping.pin;
  if ($('gpioInputFile')) $('gpioInputFile').value = mapping.file || '';
  if ($('gpioInputPlayDuration')) $('gpioInputPlayDuration').value = mapping.playDuration !== undefined ? mapping.playDuration : 0;
  if ($('gpioInputRepeatCount')) $('gpioInputRepeatCount').value = mapping.repeatCount !== undefined ? mapping.repeatCount : 0;
  if ($('gpioInputOutputPin')) $('gpioInputOutputPin').value = mapping.outputPin !== undefined ? mapping.outputPin : '0';
  if ($('gpioInputVolume')) {
    const vol = mapping.volume !== undefined ? mapping.volume : 20;
    $('gpioInputVolume').value = vol;
    if ($('gpioInputVolVal')) $('gpioInputVolVal').textContent = vol;
  }
  
  if ($('gpioInputOutputMode')) {
    const dur = mapping.outputDuration !== undefined ? mapping.outputDuration : 0;
    if (dur > 0) {
      $('gpioInputOutputMode').value = 'pulse';
      if ($('gpioInputOutputDuration')) $('gpioInputOutputDuration').value = dur;
    } else if (dur === -1) {
      $('gpioInputOutputMode').value = '-1';
    } else {
      $('gpioInputOutputMode').value = '0';
    }
    toggleGpioOutputModeFields();
  }
  
  if ($('gpioInputSaveBtn')) $('gpioInputSaveBtn').innerHTML = '<i class="fas fa-save"></i> تعديل المدخل الذكي';
  if ($('gpioInputCancelBtn')) $('gpioInputCancelBtn').style.display = 'inline-block';
}

function cancelEditGpioInput() {
  editingInputPin = null;
  editingInputOutputPin = null;
  if ($('gpioInputName')) $('gpioInputName').value = '';
  if ($('gpioInputFile')) $('gpioInputFile').selectedIndex = 0;
  if ($('gpioInputPlayDuration')) $('gpioInputPlayDuration').value = 0;
  if ($('gpioInputRepeatCount')) $('gpioInputRepeatCount').value = 0;
  if ($('gpioInputOutputPin')) $('gpioInputOutputPin').value = '0';
  if ($('gpioInputOutputMode')) {
    $('gpioInputOutputMode').value = '0';
    toggleGpioOutputModeFields();
  }
  if ($('gpioInputVolume')) {
    $('gpioInputVolume').value = 20;
    if ($('gpioInputVolVal')) $('gpioInputVolVal').textContent = 20;
  }
  if ($('gpioInputSaveBtn')) $('gpioInputSaveBtn').innerHTML = '<i class="fas fa-save"></i> حفظ المدخل الذكي';
  if ($('gpioInputCancelBtn')) $('gpioInputCancelBtn').style.display = 'none';
  populateGpioPins();
}

function deleteGpioInput(pin) {
  if (!confirm('هل أنت متأكد من حذف هذا المدخل؟')) return;
  apiPost(`/api/gpio/input/delete?pin=${pin}`, { pin }).then(() => {
    cancelEditGpioInput();
    loadGpioMappings();
  }).catch((err) => toast(`فشل الحذف: ${err.message}`));
}

function toggleGpioOutputModeFields() {
  const mode = $('gpioInputOutputMode')?.value;
  const container = $('gpioInputPulseContainer');
  if (container) {
    container.style.display = mode === 'pulse' ? 'block' : 'none';
  }
}

function loadGpioMappings() {
  apiGet('/api/gpio/mappings', {}).then((data) => {
    appState.gpioMappings = data;
    const inputs = data.inputs || [];
    if (!$('gpioInputsList')) return;
    if (inputs.length === 0) {
      $('gpioInputsList').innerHTML = '<p style="text-align:center; opacity:0.7;">لا توجد مدخلات مضافة بعد</p>';
      populateGpioPins();
      return;
    }
    $('gpioInputsList').innerHTML = inputs.map((inp) => {
      let info = `<strong>دبوس ${inp.pin}</strong>`;
      if (inp.name) info += ` (${safeText(inp.name)})`;
      if (inp.file) info += ` 🎵 ${safeText(inp.file)}`;
      if (inp.playDuration > 0 || inp.repeatCount > 0) info += ` (مدة ${inp.playDuration}ث، تكرار ${inp.repeatCount})`;
      if (inp.outputPin && Number(inp.outputPin) !== 0) {
        info += ` 🔌 مخرج ${inp.outputPin}`;
        if (inp.outputDuration > 0) info += ` (نبضة ${inp.outputDuration}ث)`;
        else if (inp.outputDuration === -1) info += ` (تبديل)`;
        else info += ` (مستمر)`;
      }
      const pinVal = Number(inp.pin);
      return `<li class="file-item" style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px; margin-bottom:10px;">
        <span>${info}</span>
        <div style="display:flex; gap:8px;">
          <button class="btn btn-secondary" onclick="editGpioInput(${pinVal})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-edit"></i> تعديل</button>
          <button class="btn btn-danger" onclick="deleteGpioInput(${pinVal})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-trash"></i> حذف</button>
        </div>
      </li>`;
    }).join('');
    populateGpioPins();
  }).catch((err) => console.error('Failed to load GPIO mappings:', err));
}

function saveGpioSched() {
  const name = $('gpioSchedName')?.value || '';
  const pin = $('gpioSchedPin')?.value || '';
  if (!pin || pin === '') return toast('يجب اختيار دبوس (Pin) صالح');
  
  const [startHour = '0', startMin = '0'] = ($('gpioSchedStart')?.value || '00:00').split(':');
  const [endHour = '0', endMin = '0'] = ($('gpioSchedEnd')?.value || '00:00').split(':');
  const type = $('gpioSchedType')?.value || 'daily';
  
  let dayOfWeek = -1, dayOfMonth = -1;
  if (type === 'weekly') dayOfWeek = getSelectedDaysBitmask('gpioSched');
  if (type === 'monthly') dayOfMonth = $('gpioSchedDay')?.value || '-1';
  
  const alertFile = $('gpioSchedAlertFile')?.value || '';
  const audioMode = $('gpioSchedAudioMode')?.value || 'original';
  const repeatCount = (audioMode === 'repeat' && alertFile) ? parseInt($('gpioSchedRepeatCount')?.value || '1') : 0;
  const volume = parseInt($('gpioSchedVolume')?.value || '20');
  
  const data = {
    name,
    pin,
    state: $('gpioSchedState')?.value || '1',
    type,
    startHour,
    startMin,
    endHour,
    endMin,
    dayOfWeek,
    dayOfMonth,
    specificDate: $('gpioSchedDate')?.value || '',
    alertFile,
    repeatCount,
    volume,
    index: editingGpioSchedIndex
  };

  apiPost('/api/gpio/schedule/add', data).then(() => {
    toast(editingGpioSchedIndex >= 0 ? 'تم تعديل جدولة المخرج بنجاح' : 'تم إضافة جدولة المخرج بنجاح');
    cancelEditGpioSched();
    loadGpioSchedules();
  }).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function cancelEditGpioSched() {
  editingGpioSchedIndex = -1;
  if ($('gpioSchedName')) $('gpioSchedName').value = '';
  if ($('gpioSchedPin')) $('gpioSchedPin').selectedIndex = 0;
  if ($('gpioSchedState')) $('gpioSchedState').value = '1';
  if ($('gpioSchedType')) {
    $('gpioSchedType').value = 'daily';
    toggleScheduleFields('gpioSched');
  }
  if ($('gpioSchedStart')) $('gpioSchedStart').value = '00:00';
  if ($('gpioSchedEnd')) $('gpioSchedEnd').value = '00:00';
  if ($('gpioSchedAlertFile')) {
    $('gpioSchedAlertFile').value = '';
    toggleGpioFileSelected('gpioSched');
  }
  if ($('gpioSchedAudioMode')) {
    $('gpioSchedAudioMode').value = 'original';
    toggleGpioAudioModeFields('gpioSched');
  }
  if ($('gpioSchedRepeatCount')) $('gpioSchedRepeatCount').value = 1;
  if ($('gpioSchedSaveBtn')) $('gpioSchedSaveBtn').innerHTML = '<i class="fas fa-save"></i> حفظ جدولة GPIO';
  if ($('gpioSchedCancelEditBtn')) $('gpioSchedCancelEditBtn').style.display = 'none';
}

function editGpioSched(index) {
  const sched = appState.gpioSchedules[index];
  if (!sched) return;
  
  editingGpioSchedIndex = index;
  
  if ($('gpioSchedName')) $('gpioSchedName').value = sched.name || '';
  if ($('gpioSchedPin')) $('gpioSchedPin').value = sched.pin;
  if ($('gpioSchedState')) $('gpioSchedState').value = sched.state ? '1' : '0';
  if ($('gpioSchedType')) {
    $('gpioSchedType').value = sched.type || 'daily';
    toggleScheduleFields('gpioSched');
  }
  
  if ($('gpioSchedStart')) {
    $('gpioSchedStart').value = `${String(sched.startHour).padStart(2, '0')}:${String(sched.startMin).padStart(2, '0')}`;
  }
  if ($('gpioSchedEnd')) {
    $('gpioSchedEnd').value = `${String(sched.endHour).padStart(2, '0')}:${String(sched.endMin).padStart(2, '0')}`;
  }
  if ($('gpioSchedAlertFile')) {
    $('gpioSchedAlertFile').value = sched.alertFile || '';
    toggleGpioFileSelected('gpioSched');
  }
  if ($('gpioSchedAudioMode')) {
    const isRepeat = (sched.repeatCount && sched.repeatCount > 0);
    $('gpioSchedAudioMode').value = isRepeat ? 'repeat' : 'original';
    toggleGpioAudioModeFields('gpioSched');
  }
  if ($('gpioSchedRepeatCount')) {
    $('gpioSchedRepeatCount').value = (sched.repeatCount && sched.repeatCount > 0) ? sched.repeatCount : 1;
  }
  if ($('gpioSchedVolume')) {
    const vol = sched.volume !== undefined ? sched.volume : 20;
    $('gpioSchedVolume').value = vol;
    if ($('gpioSchedVolVal')) $('gpioSchedVolVal').textContent = vol;
  }
  
  if (sched.type === 'weekly' && sched.dayOfWeek >= 0) {
    const mask = sched.dayOfWeek;
    document.querySelectorAll('.gpioSched-weekly-day-cb').forEach(cb => {
      const dayVal = parseInt(cb.value);
      if (mask >= 128) {
        cb.checked = !!((mask & 0x7F) & (1 << dayVal));
      } else {
        cb.checked = (dayVal === mask);
      }
    });
  } else if (sched.type === 'monthly' && $('gpioSchedDay')) {
    $('gpioSchedDay').value = sched.dayOfMonth;
  } else if ((sched.type === 'specific' || sched.type === 'yearly') && $('gpioSchedDate')) {
    $('gpioSchedDate').value = sched.specificDate || '';
  }
  
  if ($('gpioSchedSaveBtn')) $('gpioSchedSaveBtn').innerHTML = '<i class="fas fa-save"></i> تعديل جدولة GPIO';
  if ($('gpioSchedCancelEditBtn')) $('gpioSchedCancelEditBtn').style.display = 'block';
}

function loadGpioSchedules() {
  apiGet('/api/gpio/schedule/list', []).then((data) => {
    const schedules = Array.isArray(data) ? data : [];
    appState.gpioSchedules = schedules;
    if (!$('gpioSchedulesList')) return;
    $('gpioSchedulesList').innerHTML = schedules.map((s, i) => {
      let info = `<strong>${safeText(s.name || 'جدولة مخرج')}</strong> (دبوس ${s.pin})`;
      info += `<br><span style="font-size:0.9em;opacity:0.8;">⚡ الحالة: ${s.state ? 'تشغيل' : 'إيقاف'} | 🕒 ${safeText(s.type)}`;
      if (s.type === 'weekly' && s.dayOfWeek !== undefined) info += ` (${formatDays(s.dayOfWeek)})`;
      if (s.type === 'monthly') info += ` (يوم ${s.dayOfMonth})`;
      if (s.type === 'specific') info += ` (${safeText(s.specificDate)})`;
      if (s.type === 'yearly') info += ` (سنوياً في ${safeText(s.specificDate ? s.specificDate.substring(5) : '')})`;
      info += ` | الوقت: ${String(s.startHour).padStart(2, '0')}:${String(s.startMin).padStart(2, '0')} إلى ${String(s.endHour).padStart(2, '0')}:${String(s.endMin).padStart(2, '0')}`;
      if (s.alertFile) {
        info += ` | 🔊 صوت: ${safeText(s.alertFile)}`;
        if (s.repeatCount && s.repeatCount > 0) {
          info += ` (تكرار: ${s.repeatCount} مرات)`;
        } else {
          info += ` (المدة الأصلية)`;
        }
      }
      info += '</span>';
      
      return `<li class="file-item" style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px; margin-bottom:10px;">
        <span>${info}</span>
        <div style="display:flex; gap:8px;">
          <button class="btn btn-secondary" onclick="editGpioSched(${i})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-edit"></i> تعديل</button>
          <button class="btn btn-danger" onclick="deleteGpioSched(${i})" style="padding:4px 8px; font-size:12px;"><i class="fas fa-trash"></i> حذف</button>
        </div>
      </li>`;
    }).join('');
    populateGpioPins();
  }).catch((err) => console.error('Failed to load GPIO schedules:', err));
}

function deleteGpioSched(index) {
  if (!confirm('هل أنت متأكد من حذف هذه الجدولة؟')) return;
  apiPost(`/api/gpio/schedule/delete?index=${index}`, { index }).then(() => {
    cancelEditGpioSched();
    loadGpioSchedules();
  }).catch((err) => toast(`فشل الحذف: ${err.message}`));
}

function toggleEidMode() {
  apiPost('/api/eid/mode', { enabled: $('eidModeToggle')?.checked ? '1' : '0' })
    .then(() => {
      updateEidModeBanner($('eidModeToggle')?.checked === true);
      toast('تم تحديث وضع العيد');
    }).catch((err) => toast(`فشل التحديث: ${err.message}`));
}

function updateEidModeBanner(enabled) {
  if ($('eidModeBanner')) $('eidModeBanner').style.display = enabled ? 'flex' : 'none';
}

function loadEidModeStatus() {
  apiGet('/api/eid/status', {}).then((data) => {
    const enabled = !!data.enabled;
    if ($('eidModeToggle')) $('eidModeToggle').checked = enabled;
    if ($('eidTakbeerFile') && data.takbeerFile) $('eidTakbeerFile').value = data.takbeerFile;
    if ($('eidTakbeerVolume') && data.takbeerVolume !== undefined) {
      $('eidTakbeerVolume').value = data.takbeerVolume;
      if ($('eidTakbeerVolumeValue')) $('eidTakbeerVolumeValue').textContent = data.takbeerVolume;
    }
    updateEidModeBanner(enabled);
  });
}

function triggerTakbeer() {
  apiPost('/api/audio/play', {
    file: $('eidTakbeerFile')?.value || '',
    priority: 1,
    volume: $('eidTakbeerVolume')?.value || 15
  })
    .catch((err) => toast(`فشل التشغيل: ${err.message}`));
}

function saveEidFile() {
  apiPost('/api/eid/file', {
    file: $('eidTakbeerFile')?.value || '',
    volume: $('eidTakbeerVolume')?.value || 15
  })
    .then(() => toast('تم حفظ ملف التكبيرات')).catch((err) => toast(`فشل الحفظ: ${err.message}`));
}

function playSingleFile() {
  const file = $('playlistFileSelect')?.value || '';
  if (!file) return toast('اختر ملفاً أولاً');
  const volume = $('playlistVolume')?.value || 15;
  apiPost('/api/audio/play', { file: file, priority: 0, volume: volume })
    .then(() => {
      if ($('playerControlsContainer')) $('playerControlsContainer').style.display = 'block';
      if ($('dashPlayerControlsContainer')) $('dashPlayerControlsContainer').style.display = 'block';
    })
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
  }).then(() => {
    if ($('playerControlsContainer')) $('playerControlsContainer').style.display = 'block';
    if ($('dashPlayerControlsContainer')) $('dashPlayerControlsContainer').style.display = 'block';
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
          <select id="maghribFile_${i}" style="width:100%;">${appState.files.filter(f => !f.isDirectory && !String(f.name || '').startsWith('prayer_csv/') && /\.(mp3|wav)$/i.test(f.name || '')).map(f => `<option value="${safeAttr(f.name)}" ${f.name === a.file ? 'selected' : ''}>${safeText(f.name)}</option>`).join('')}</select>
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
  const btn = $('otaBtn');
  const container = $('otaProgressContainer');
  const bar = $('otaProgressBar');
  const status = $('otaStatusText');

  if (!input?.files?.length) return toast('اختر ملف التحديث');
  
  const file = input.files[0];
  const body = new FormData();
  body.append('update', file);
  if (appState.sessionToken) body.append('token', appState.sessionToken);

  const otaUrl = appState.sessionToken ? `/api/ota?token=${encodeURIComponent(appState.sessionToken)}` : '/api/ota';

  if (btn) btn.disabled = true;
  if (container) container.style.display = 'block';
  if (status) {
    status.style.display = 'block';
    status.innerHTML = 'جاري بدء التحديث...';
  }
  if (bar) bar.style.width = '0%';

  const xhr = new XMLHttpRequest();
  xhr.open('POST', otaUrl, true);

  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      if (bar) bar.style.width = `${pct}%`;
      if (status) status.innerHTML = `جاري رفع التحديث: ${pct}%`;
    }
  };

  xhr.onload = function() {
    if (xhr.status === 200) {
      if (bar) bar.style.width = '100%';
      if (status) status.innerHTML = 'تم رفع التحديث بنجاح! جاري إعادة تشغيل الجهاز...';
      toast('تم رفع التحديث بنجاح! سيعاد تشغيل الجهاز بعد قليل.');
      setTimeout(() => {
        location.reload();
      }, 5000);
    } else if (xhr.status === 401) {
      doLogout();
      toast('غير مصرح لك بالوصول، يرجى تسجيل الدخول مجدداً.');
      resetOtaUI();
    } else {
      toast('فشل التحديث: حدث خطأ أثناء الرفع.');
      resetOtaUI();
    }
  };

  xhr.onerror = function() {
    toast('فشل التحديث: خطأ في الاتصال بالشبكة.');
    resetOtaUI();
  };

  xhr.send(body);

  function resetOtaUI() {
    if (btn) btn.disabled = false;
    if (container) container.style.display = 'none';
    if (status) status.style.display = 'none';
    if (input) input.value = '';
  }
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

function toggleCalendarOnly() {
  apiPost('/api/calendar/only', { enabled: $('calendarOnlyToggle')?.checked ? '1' : '0' })
    .then(loadCsvStatus).catch((err) => toast(`فشل التحديث: ${err.message}`));
}

function toggleCalendarFallback() {
  apiPost('/api/calendar/fallback', { enabled: $('calendarFallbackToggle')?.checked ? '1' : '0' })
    .then(loadCsvStatus).catch((err) => toast(`فشل التحديث: ${err.message}`));
}

let calendarDownloadActive = false;
let calendarMissingMonths = [];

function monthLabel(month) {
  const names = ['يناير','فبراير','مارس','أبريل','مايو','يونيو','يوليو','أغسطس','سبتمبر','أكتوبر','نوفمبر','ديسمبر'];
  return names[month - 1] || String(month);
}

function renderCalendarMonths(year, available, missing) {
  if (!$('calendarMonthsView')) return;
  const availableText = available.length ? available.map((m) =>
    `<button class="btn" style="padding:6px 10px; margin:3px;" onclick="openCalendarMonth(${year}, ${m})">${safeText(monthLabel(m))}</button>`
  ).join('') : 'لا يوجد';
  const missingText = missing.length ? missing.map(monthLabel).join('، ') : 'لا يوجد';
  $('calendarMonthsView').innerHTML =
    `<div>الموجود على SD لسنة ${year}: <span style="color:#2ecc71">${availableText}</span></div>` +
    `<div>الناقص: <span style="color:${missing.length ? '#e74c3c' : '#2ecc71'}">${safeText(missingText)}</span></div>`;
}

function openCalendarMonth(year, month) {
  apiGet(`/api/calendar/month?year=${encodeURIComponent(year)}&month=${encodeURIComponent(month)}`, { ok: false }, 0)
    .then((data) => {
      if (!data.ok) return toast('تعذر فتح ملف الشهر');
      calendarEditorYear = year;
      calendarEditorMonth = month;
      if ($('calendarMonthEditor')) $('calendarMonthEditor').style.display = 'block';
      if ($('calendarMonthEditorTitle')) $('calendarMonthEditorTitle').textContent = `محتوى ${monthLabel(month)} ${year}`;
      if ($('calendarMonthCsv')) $('calendarMonthCsv').value = data.csv || '';
    });
}

function saveCalendarMonthCsv() {
  if (!calendarEditorYear || !calendarEditorMonth) return toast('اختر شهر أولاً');
  apiPost('/api/calendar/month', {
    year: calendarEditorYear,
    month: calendarEditorMonth,
    csv: $('calendarMonthCsv')?.value || ''
  }).then(() => {
    toast('تم حفظ تعديل الشهر');
    loadCsvStatus();
    fetchPrayerTimes();
  }).catch((err) => toast(`فشل حفظ الشهر: ${err.message}`));
}

function closeCalendarMonthEditor() {
  if ($('calendarMonthEditor')) $('calendarMonthEditor').style.display = 'none';
  calendarEditorYear = 0;
  calendarEditorMonth = 0;
}

function downloadCalendarMonths(months, force = false) {
  const year = $('calendarYearInput')?.value || new Date().getFullYear();
  const country = $('countrySelect')?.value || '';
  const city = $('citySelect')?.value || '';
  const method = $('methodSelect')?.value || defaultPrayerMethod(country);
  if (!country || !city) return toast('اختر الدولة والمدينة أولاً');
  if (calendarDownloadActive) return toast('يوجد تحميل رزنامة قيد التنفيذ');
  if (!months.length) return toast('لا توجد شهور ناقصة للتحميل');
  calendarDownloadActive = true;
  let saved = 0;
  let skipped = 0;
  const failed = [];
  if ($('calendarStatus')) $('calendarStatus').textContent = `جاري تحميل الرزنامة إلى SD... 0/${months.length}`;

  const waitCalendarJob = (month) => new Promise((resolve) => {
    const poll = () => {
      apiGet('/api/calendar/download_status', { ok: false }, 0)
        .then((status) => {
          if (!status.done) {
            setTimeout(poll, 600);
            return;
          }
          if (status.success) saved++;
          else failed.push(`${month}${status.error ? ':' + status.error : ''}`);
          if ($('calendarStatus')) $('calendarStatus').textContent = `جاري التحميل... ${month}/12 (تم ${saved})`;
          resolve();
        })
        .catch(() => {
          failed.push(`${month}:status_failed`);
          resolve();
        });
    };
    poll();
  });

  const downloadMonth = (month, index) => apiPost('/api/calendar/download_month', { year, month, country, city, method, force: force ? '1' : '0' })
    .then((data) => {
      if (data.skipped) {
        skipped++;
        if ($('calendarStatus')) $('calendarStatus').textContent = `تخطي ${monthLabel(month)} لأنه موجود (${index + 1}/${months.length})`;
        return;
      }
      if (!data.ok) {
        failed.push(`${month}${data.error ? ':' + data.error : ''}`);
        return;
      }
      return waitCalendarJob(month);
    })
    .catch((err) => {
      failed.push(`${month}:${err.message}`);
      if ($('calendarStatus')) $('calendarStatus').textContent = `فشل شهر ${month}: ${err.message}`;
    });

  let chain = Promise.resolve();
  months.forEach((month, index) => {
    chain = chain.then(() => downloadMonth(month, index));
  });
  chain.then(() => {
    calendarDownloadActive = false;
    if (failed.length === 0) {
      toast('اكتمل تحميل الرزنامة');
      if ($('calendarStatus')) $('calendarStatus').textContent = `اكتمل: تم ${saved}، تم تخطي ${skipped}`;
    } else {
      toast(`اكتمل جزئياً: تم ${saved} وفشل ${failed.length}`);
      if ($('calendarStatus')) $('calendarStatus').textContent = `اكتمل جزئياً. تم ${saved}، تخطي ${skipped}. فشل: ${failed.join(', ')}`;
    }
    loadCsvStatus();
  });
}

function downloadYearCalendar() {
  downloadCalendarMonths([1,2,3,4,5,6,7,8,9,10,11,12], false);
}

function downloadMissingCalendarMonths() {
  downloadCalendarMonths(calendarMissingMonths.slice(), false);
}

function deleteYearCalendar() {
  if (calendarDownloadActive) return toast('أوقف/انتظر انتهاء التحميل الحالي أولاً');
  const year = $('calendarYearInput')?.value || new Date().getFullYear();
  if (!confirm(`حذف رزنامة سنة ${year} من SD؟`)) return;
  apiPost('/api/calendar/delete_year', { year })
    .then((data) => {
      if (!data.ok) throw new Error(data.error || 'delete failed');
      toast('تم حذف رزنامة السنة من SD');
      if ($('calendarStatus')) $('calendarStatus').textContent = `تم حذف رزنامة ${year}`;
      loadCsvStatus();
    })
    .catch((err) => toast(`فشل الحذف: ${err.message}`));
}

function loadCsvStatus() {
  const selectedYear = $('calendarYearInput')?.value || '';
  const query = selectedYear ? `?year=${encodeURIComponent(selectedYear)}` : '';
  apiGet(`/api/csv/status${query}`, {}).then((data) => {
    if ($('csvModeToggle')) $('csvModeToggle').checked = !!data.enabled;
    if ($('calendarOnlyToggle')) $('calendarOnlyToggle').checked = !!data.calendarOnly;
    if ($('calendarFallbackToggle')) $('calendarFallbackToggle').checked = data.calendarFallback !== false;
    if ($('calendarYearInput') && data.calendarYear) $('calendarYearInput').value = data.calendarYear;
    if (data.deferred) {
      appState.csvStatusLoaded = false;
      return;
    }
    appState.csvStatusLoaded = true;
    if ($('calendarStatus')) {
      const months = data.calendarMonths || [];
      calendarMissingMonths = data.missingCalendarMonths || [];
      $('calendarStatus').textContent = months.length
        ? `رزنامة ${data.calendarYear}: ${months.length}/12 شهر على SD`
        : 'لا توجد رزنامة سنوية محفوظة للسنة الحالية';
      renderCalendarMonths(data.calendarYear, months, calendarMissingMonths);
    }
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

  const timeContainer = $('eidScheduleTimeContainer');
  if (timeContainer) {
    timeContainer.style.display = type === 'prayer_relative' ? 'none' : 'block';
  }
}

function toggleEidLoopFields() {
  if ($('eidLoopFields')) $('eidLoopFields').style.display = $('eidScheduleLoopToggle')?.value === 'yes' ? 'block' : 'none';
}

function addEidSchedule() {
  const checkboxes = document.querySelectorAll('.eid-file-cb:checked');
  if (checkboxes.length === 0) return toast('اختر ملفاً واحداً على الأقل');
  const fileList = Array.from(checkboxes).map(cb => cb.value).join(',');

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

  const repeatInterval = Number($('eidScheduleRepeatInterval')?.value || 0);
  const endHour = $('eidScheduleEndTime')?.value ? Number($('eidScheduleEndTime').value.split(':')[0]) : -1;
  const endMinute = $('eidScheduleEndTime')?.value ? Number($('eidScheduleEndTime').value.split(':')[1]) : -1;

  apiPost('/api/scheduler/add', {
    file: fileList,
    type,
    hour,
    minute,
    dayOfWeek,
    dayOfMonth,
    specificDate: $('eidScheduleDate')?.value || '',
    volume: $('eidScheduleVolume')?.value || 20,
    loop: $('eidScheduleLoopToggle')?.value === 'yes' ? Number($('eidScheduleLoopDuration')?.value || 0) * 60 : 0,
    prayerIndex: $('eidSchedulePrayer')?.value || 0,
    offsetSeconds,
    eidOnly: '1',
    important: 1,
    repeatInterval,
    endHour,
    endMinute
  }).then(() => {
    toast('تم إضافة تنبيه العيد');
    document.querySelectorAll('.eid-file-cb').forEach(cb => cb.checked = false);
    if ($('eidScheduleRepeatInterval')) $('eidScheduleRepeatInterval').value = 0;
    if ($('eidScheduleEndTime')) $('eidScheduleEndTime').value = '';
    toggleAlertEndTimeField('eidSchedule');
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
          if (a.repeatInterval > 0) {
            info += ` (تكرار كل ${a.repeatInterval}د`;
            if (a.endHour >= 0 && a.endMinute >= 0) {
              info += ` حتى ${String(a.endHour).padStart(2, '0')}:${String(a.endMinute).padStart(2, '0')}`;
            }
            info += `)`;
          }
          info += ` - صوت: ${a.volume || 20}`;
          if (a.loop > 0) info += ` (تكرار ${Math.round(a.loop / 60)}د)`;
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
  const takbeerFile = $('eidTakbeerFile')?.value || '';
  if (!takbeerFile) return toast('اختر ملف التكبيرات أولاً');
  const takbeerVolume = $('eidTakbeerVolume')?.value || 15;

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
  apiPost('/api/eid/file', { file: takbeerFile, volume: takbeerVolume })
    .then(() => apiPost('/api/eid/takbeer_config/save', {json: JSON.stringify(prayers)}))
    .then(() => toast('تم حفظ ملف التكبيرات وجدولتها'))
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
  apiPost('/api/password/change', { old: oldPassword, password: newPassword, token: appState.sessionToken })
    .then((data) => {
      if (!data.ok) return toast('كلمة المرور القديمة غير صحيحة');
      appState.sessionToken = data.token || appState.sessionToken;
      localStorage.setItem('vivoSessionToken', appState.sessionToken);
      toast('تم تغيير كلمة المرور');
    })
    .catch((err) => toast(`فشل التغيير: ${err.message}`));
}

document.addEventListener('DOMContentLoaded', () => {
  const isMobileDevice = /Mobi|Android|iPhone|iPad|iPod/i.test(navigator.userAgent);
  if (isMobileDevice) {
    document.body.classList.add('is-mobile');
  }
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
  const hasToken = appState.sessionToken || localStorage.getItem('vivoPassword');

  if (sessionValid && hasToken) {
      document.body.classList.add('logged-in');
      const overlay = $('loginOverlay');
      if (overlay) overlay.style.display = 'none';
      const main = $('mainContent');
      if (main) main.style.display = 'block';
      initDashboard();
  }

  if ($('eidScheduleVolume')) {
    $('eidScheduleVolume').addEventListener('input', () => {
      if ($('eidScheduleVolumeValue')) $('eidScheduleVolumeValue').textContent = $('eidScheduleVolume').value;
    });
  }
  if ($('eidTakbeerVolume')) {
    $('eidTakbeerVolume').addEventListener('input', () => {
      if ($('eidTakbeerVolumeValue')) $('eidTakbeerVolumeValue').textContent = $('eidTakbeerVolume').value;
    });
  }

  let prayerFetchCounter = 0;
  setInterval(() => {
    if ($('mainContent')?.style.display !== 'none') {
      updateClock();
      fetchStatus();
      fetchTrackInfo();
      checkSessionTimeout();
      prayerFetchCounter++;
      if (!calendarDownloadActive && prayerFetchCounter % 6 === 0 && appState.activeTab === 'dashboard') fetchPrayerTimes();
    }
  }, 2000);
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

// Event Log Management Logic
let allLogs = [];

function fetchLogs() {
  const tbody = $('logsTableBody');
  if (tbody) {
    tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; padding: 20px; font-weight: bold; color: var(--primary-blue);">جاري تحميل سجل الأحداث...</td></tr>';
  }
  
  fetch('/api/logs?limit=200')
    .then(res => res.json())
    .then(data => {
      allLogs = Array.isArray(data) ? data : [];
      filterLogs();
    })
    .catch(err => {
      console.error('Error fetching logs:', err);
      if (tbody) {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--color-danger); padding: 20px; font-weight: bold;">فشل في تحميل السجلات. تأكد من اتصالك بالشبكة.</td></tr>';
      }
    });
}

function filterLogs() {
  const searchQuery = ($('logSearchInput')?.value || '').toLowerCase().trim();
  const levelFilter = $('logLevelFilter')?.value || 'ALL';
  const tbody = $('logsTableBody');
  const noData = $('logsNoData');
  
  if (!tbody) return;
  
  const filtered = allLogs.filter(log => {
    // Level filter
    if (levelFilter !== 'ALL' && log.level.toUpperCase() !== levelFilter.toUpperCase()) {
      return false;
    }
    // Search query filter
    if (searchQuery) {
      const msgMatch = (log.message || '').toLowerCase().includes(searchQuery);
      const catMatch = (log.category || '').toLowerCase().includes(searchQuery);
      const tsMatch = (log.timestamp || '').toLowerCase().includes(searchQuery);
      return msgMatch || catMatch || tsMatch;
    }
    return true;
  });
  
  // Sort in reverse chronological order (newest first)
  filtered.sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));
  
  tbody.innerHTML = '';
  if (filtered.length === 0) {
    if (noData) noData.style.display = 'block';
    return;
  }
  if (noData) noData.style.display = 'none';
  
  filtered.forEach(log => {
    const tr = document.createElement('tr');
    
    const tdTs = document.createElement('td');
    tdTs.textContent = log.timestamp;
    tdTs.style.padding = '12px 10px';
    tr.appendChild(tdTs);
    
    const tdLvl = document.createElement('td');
    tdLvl.style.textAlign = 'center';
    tdLvl.style.padding = '12px 10px';
    const badge = document.createElement('span');
    badge.className = `log-level-badge log-level-${log.level.toLowerCase()}`;
    badge.textContent = log.level;
    tdLvl.appendChild(badge);
    tr.appendChild(tdLvl);
    
    const tdCat = document.createElement('td');
    tdCat.textContent = log.category;
    tdCat.style.padding = '12px 10px';
    tr.appendChild(tdCat);
    
    const tdMsg = document.createElement('td');
    tdMsg.textContent = log.message;
    tdMsg.style.padding = '12px 10px';
    tdMsg.style.whiteSpace = 'normal';
    tdMsg.style.wordBreak = 'break-all';
    tr.appendChild(tdMsg);
    
    const tdHeap = document.createElement('td');
    tdHeap.textContent = Number(log.freeHeap).toLocaleString();
    tdHeap.style.padding = '12px 10px';
    tdHeap.style.textAlign = 'left';
    tr.appendChild(tdHeap);
    
    const tdUptime = document.createElement('td');
    tdUptime.textContent = Number(log.uptime).toLocaleString();
    tdUptime.style.padding = '12px 10px';
    tdUptime.style.textAlign = 'left';
    tr.appendChild(tdUptime);
    
    tbody.appendChild(tr);
  });
}

function clearLogs() {
  const pass = prompt('الرجاء إدخال كلمة مرور الإدارة لمسح جميع السجلات:');
  if (pass === null) return;
  if (!pass.trim()) {
    alert('كلمة المرور فارغة!');
    return;
  }
  
  const formData = new FormData();
  formData.append('password', pass);
  
  fetch('/api/logs', {
    method: 'DELETE',
    body: formData
  })
  .then(res => res.json())
  .then(data => {
    if (data.ok) {
      alert('تم مسح جميع السجلات بنجاح.');
      allLogs = [];
      filterLogs();
    } else {
      alert('فشل المسح: ' + (data.error || 'كلمة المرور غير صحيحة'));
    }
  })
  .catch(err => {
    console.error('Error clearing logs:', err);
    alert('حدث خطأ أثناء محاولة مسح السجلات.');
  });
}

function exportLogs(format) {
  if (allLogs.length === 0) {
    alert('لا توجد سجلات لتصديرها!');
    return;
  }
  
  let output = '';
  let filename = `system_logs_${new Date().toISOString().split('T')[0]}`;
  let mimeType = 'text/plain';
  
  const searchQuery = ($('logSearchInput')?.value || '').toLowerCase().trim();
  const levelFilter = $('logLevelFilter')?.value || 'ALL';
  const filtered = allLogs.filter(log => {
    if (levelFilter !== 'ALL' && log.level.toUpperCase() !== levelFilter.toUpperCase()) return false;
    if (searchQuery) {
      const msgMatch = (log.message || '').toLowerCase().includes(searchQuery);
      const catMatch = (log.category || '').toLowerCase().includes(searchQuery);
      const tsMatch = (log.timestamp || '').toLowerCase().includes(searchQuery);
      return msgMatch || catMatch || tsMatch;
    }
    return true;
  });
  
  filtered.sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));
  
  if (format === 'csv') {
    filename += '.csv';
    mimeType = 'text/csv';
    output = '\uFEFF'; // Excel Arabic support BOM
    output += 'Timestamp,Level,Category,Message,Free Heap (Bytes),Uptime (Seconds)\n';
    filtered.forEach(log => {
      const escapedMsg = `"${(log.message || '').replace(/"/g, '""')}"`;
      const escapedCat = `"${(log.category || '').replace(/"/g, '""')}"`;
      output += `${log.timestamp},${log.level},${escapedCat},${escapedMsg},${log.freeHeap},${log.uptime}\n`;
    });
  } else {
    filename += '.txt';
    filtered.forEach(log => {
      output += `[${log.timestamp}] [${log.level}] [${log.category}] ${log.message} (Heap: ${log.freeHeap}, Uptime: ${log.uptime}s)\r\n`;
    });
  }
  
  const blob = new Blob([output], { type: `${mimeType};charset=utf-8;` });
  const link = document.createElement('a');
  if (link.download !== undefined) {
    const url = URL.createObjectURL(blob);
    link.setAttribute('href', url);
    link.setAttribute('download', filename);
    link.style.visibility = 'hidden';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }
}

function downloadRawLogFile() {
  const link = document.createElement('a');
  link.href = '/api/logs/download';
  link.download = '';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

