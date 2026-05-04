// WebPages.h
#ifndef WEBPAGES_H
#define WEBPAGES_H

#include <Arduino.h>

const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Vivo Smart – نظام المنزل الذكي</title>
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
  <style>
    :root {
      --glass-bg: rgba(255, 255, 255, 0.08);
      --glass-border: rgba(255, 255, 255, 0.2);
      --primary: #2ec4b6;
      --danger: #e63946;
      --warning: #f4a261;
      --bg: #0a192f;
      --card-bg: rgba(20, 30, 50, 0.7);
      --text: #fff;
      --text-secondary: #a8b2d1;
      --shadow: 0 8px 32px rgba(0,0,0,0.3);
      --radius: 16px;
    }
    body {
      margin: 0; font-family: 'Tajawal', 'Segoe UI', sans-serif;
      background: linear-gradient(135deg, #0f2027, #203a43, #2c5364);
      background-attachment: fixed; color: var(--text);
      display: flex; min-height: 100vh; overflow-x: hidden;
    }
    .sidebar {
      width: 260px; background: var(--card-bg); backdrop-filter: blur(20px);
      border-left: 1px solid var(--glass-border); padding: 20px 10px;
      display: flex; flex-direction: column; align-items: center;
      position: fixed; top: 0; bottom: 0; right: 0; z-index: 1000;
      box-shadow: 2px 0 10px rgba(0,0,0,0.5); transition: transform 0.3s;
    }
    .sidebar .logo { font-size: 26px; font-weight: bold; margin-bottom: 30px; color: var(--primary); text-align: center; }
    .sidebar .logo i { font-size: 32px; display: block; }
    .sidebar a {
      display: flex; align-items: center; gap: 12px; color: var(--text);
      text-decoration: none; padding: 12px 15px; border-radius: 12px;
      margin: 5px 0; width: 100%; font-size: 16px; transition: 0.3s; background: transparent;
    }
    .sidebar a i { width: 20px; text-align: center; }
    .sidebar a:hover, .sidebar a.active { background: rgba(46,196,182,0.3); color: var(--primary); }
    .sidebar .version { margin-top: auto; font-size: 12px; color: var(--text-secondary); }
    .main-content { flex: 1; margin-right: 260px; padding: 20px; transition: margin 0.3s; }
    @media (max-width: 768px) {
      .sidebar { transform: translateX(100%); }
      .sidebar.open { transform: translateX(0); }
      .main-content { margin-right: 0; }
      .mobile-menu-btn { display: block; }
    }
    .mobile-menu-btn { display: none; position: fixed; top: 15px; right: 15px; z-index: 1100;
      background: var(--primary); border: none; color: #fff; font-size: 22px;
      width: 40px; height: 40px; border-radius: 10px; cursor: pointer; }
    .glass-card {
      background: var(--card-bg); backdrop-filter: blur(12px);
      border: 1px solid var(--glass-border); border-radius: var(--radius);
      padding: 20px; margin-bottom: 20px; box-shadow: var(--shadow);
      transition: transform 0.2s;
    }
    .glass-card:hover { transform: translateY(-3px); }
    h1, h2, h3 { color: var(--primary); margin-top: 0; }
    .row { display: flex; flex-wrap: wrap; gap: 20px; }
    .col { flex: 1; min-width: 250px; }
    .btn {
      display: inline-flex; align-items: center; gap: 6px; background: var(--primary);
      color: #fff; border: none; padding: 10px 18px; border-radius: 25px;
      cursor: pointer; font-size: 14px; transition: 0.3s; text-decoration: none;
    }
    .btn:hover { background: #248c80; box-shadow: 0 0 15px rgba(46,196,182,0.6); }
    .btn-danger { background: var(--danger); }
    .btn-warning { background: var(--warning); color: #000; }
    .btn-outline { background: transparent; border: 1px solid var(--primary); color: var(--primary); }
    .btn-outline:hover { background: var(--primary); color: #fff; }
    input, select, textarea {
      width: 100%; padding: 12px; margin: 8px 0 16px;
      border: 1px solid var(--glass-border); border-radius: 12px;
      background: rgba(255,255,255,0.1); color: #fff; font-size: 14px; box-sizing: border-box;
    }
    label { color: var(--text-secondary); font-size: 14px; }
    .switch { position: relative; display: inline-block; width: 50px; height: 26px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; background-color: #ccc; border-radius: 34px; top: 0; left: 0; right: 0; bottom: 0; transition: 0.4s; }
    .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 3px; bottom: 3px; background: white; border-radius: 50%; transition: 0.4s; }
    input:checked + .slider { background-color: var(--primary); }
    input:checked + .slider:before { transform: translateX(24px); }
    .alert { padding: 12px; border-radius: 10px; margin: 10px 0; }
    .alert-success { background: rgba(46,196,182,0.3); border: 1px solid var(--primary); }
    .alert-danger { background: rgba(230,57,70,0.3); border: 1px solid var(--danger); }
    .tab { display: none; }
    .tab.active { display: block; }
    .divider { height: 1px; background: var(--glass-border); margin: 20px 0; }
    .file-list { max-height: 300px; overflow-y: auto; background: rgba(0,0,0,0.2); border-radius: 12px; padding: 10px; }
    .file-item { display: flex; justify-content: space-between; align-items: center; padding: 8px; border-bottom: 1px solid rgba(255,255,255,0.1); }
  </style>
</head>
<body>
  <button class="mobile-menu-btn" onclick="document.querySelector('.sidebar').classList.toggle('open')">
    <i class="fas fa-bars"></i>
  </button>
  <nav class="sidebar">
    <div class="logo"><i class="fas fa-mosque"></i> Vivo Smart</div>
    <a href="#dashboard" class="active" onclick="showTab('dashboard')"><i class="fas fa-home"></i> الرئيسية</a>
    <a href="#network" onclick="showTab('network')"><i class="fas fa-wifi"></i> الشبكة</a>
    <a href="#prayer" onclick="showTab('prayer')"><i class="fas fa-clock"></i> مواقيت الصلاة</a>
    <a href="#files" onclick="showTab('files')"><i class="fas fa-folder-open"></i> الملفات</a>
    <a href="#scheduler" onclick="showTab('scheduler')"><i class="fas fa-calendar-alt"></i> الجدولة</a>
    <a href="#gpio" onclick="showTab('gpio')"><i class="fas fa-microchip"></i> GPIO</a>
    <a href="#eid" onclick="showTab('eid')"><i class="fas fa-star-and-crescent"></i> العيد</a>
    <a href="#player" onclick="showTab('player')"><i class="fas fa-music"></i> مشغل الصوت</a>
    <a href="#maghrib" onclick="showTab('maghrib')"><i class="fas fa-sun"></i> تنبيهات المغرب</a>
    <span class="version">v2.0 ESP32-S3</span>
  </nav>

  <main class="main-content" id="mainContent">
    <!-- Dashboard -->
    <div id="tab-dashboard" class="tab active">
      <h1><i class="fas fa-tachometer-alt"></i> لوحة التحكم</h1>
      <div class="row">
        <div class="col glass-card">
          <h3><i class="far fa-clock"></i> الوقت الحالي</h3>
          <p style="font-size:28px"><span id="timeDisplay">--:--:--</span></p>
          <p><span id="hijriDate"></span> | <span id="gregDate"></span></p>
        </div>
        <div class="col glass-card">
          <h3><i class="fas fa-volume-up"></i> حالة النظام</h3>
          <p id="playingStatus">متوقف</p>
          <input type="range" id="volumeSlider" min="0" max="30" value="15" onchange="setVolume(this.value)">
          <button class="btn" onclick="stopAudio()"><i class="fas fa-stop"></i> إيقاف</button>
        </div>
      </div>
      <div class="glass-card">
        <h3>الأذان القادم</h3>
        <p id="nextPrayer"></p>
        <button class="btn" onclick="triggerAdhan('fajr')">أذان الفجر</button>
        <button class="btn" onclick="triggerAdhan('dhuhr')">أذان الظهر</button>
        <button class="btn" onclick="triggerIqama()">إقامة</button>
      </div>
    </div>

    <!-- Network -->
    <div id="tab-network" class="tab">
      <h1><i class="fas fa-wifi"></i> إعدادات الشبكة</h1>
      <div class="glass-card">
        <h3>WiFi</h3>
        <label>اسم الشبكة</label><input type="text" id="ssid">
        <label>كلمة المرور</label><input type="password" id="wifiPass">
        <button class="btn" onclick="scanWiFi()"><i class="fas fa-search"></i> بحث</button>
        <div id="wifiList" style="max-height:150px;overflow:auto;margin:10px 0"></div>
        <label class="switch"><input type="checkbox" id="dhcpToggle" onchange="toggleDHCP()" checked><span class="slider"></span></label> DHCP
        <div id="staticIPFields" style="display:none">
          <label>IP</label><input type="text" id="staticIP" placeholder="192.168.1.100">
          <label>البوابة</label><input type="text" id="gateway" placeholder="192.168.1.1">
          <label>القناع</label><input type="text" id="subnet" placeholder="255.255.255.0">
          <label>DNS</label><input type="text" id="dns" placeholder="8.8.8.8">
        </div>
        <button class="btn" onclick="saveNetwork()"><i class="fas fa-save"></i> حفظ</button>
      </div>
    </div>

    <!-- Prayer -->
    <div id="tab-prayer" class="tab">
      <h1><i class="fas fa-mosque"></i> مواقيت الصلاة</h1>
      <div class="row">
        <div class="col glass-card">
          <h3>الموقع</h3>
          <select id="countrySelect" onchange="loadCities()"><option value="EG">مصر</option></select>
          <select id="citySelect"><option value="Cairo">القاهرة</option></select>
          <select id="methodSelect"><option value="5">الهيئة المصرية</option></select>
          <button class="btn" onclick="fetchPrayerTimes()">جلب المواقيت</button>
        </div>
        <div class="col glass-card">
          <h3>الإزاحات (دقائق)</h3>
          <label>الفجر</label><input type="number" id="offsetFajr" value="0">
          <label>الظهر</label><input type="number" id="offsetDhuhr" value="0">
          <label>العصر</label><input type="number" id="offsetAsr" value="0">
          <label>المغرب</label><input type="number" id="offsetMaghrib" value="0">
          <label>العشاء</label><input type="number" id="offsetIsha" value="0">
          <button class="btn" onclick="saveOffsets()">حفظ</button>
        </div>
      </div>
    </div>

    <!-- Files -->
    <div id="tab-files" class="tab">
      <h1><i class="fas fa-folder-open"></i> الملفات</h1>
      <div class="glass-card">
        <h3>رفع ملف</h3>
        <input type="file" id="fileInput" accept=".mp3,.wav">
        <button class="btn" onclick="uploadFile()"><i class="fas fa-upload"></i> رفع</button>
        <div id="uploadStatus"></div>
        <div class="progress" style="display:none"><div id="uploadProgressBar" style="width:0%;background:var(--primary);height:8px;border-radius:4px;"></div></div>
      </div>
      <div class="glass-card">
        <h3>ملفات SD</h3>
        <div class="file-list" id="fileList"></div>
      </div>
      <div class="glass-card" id="previewCard" style="display:none">
        <h3>معاينة: <span id="previewName"></span></h3>
        <audio id="audioPlayer" controls style="width:100%"></audio>
        <button class="btn btn-outline" onclick="closePreview()">إغلاق</button>
      </div>
      <div class="glass-card">
        <h3>تعيين ملف للأذان</h3>
        <select id="adhanFileSelect"><option>اختر ملف</option></select>
        <button class="btn" onclick="assignAdhan()">تعيين أذان</button>
        <button class="btn" onclick="assignIqama()">تعيين إقامة</button>
      </div>
    </div>

    <!-- Scheduler -->
    <div id="tab-scheduler" class="tab">
      <h1><i class="fas fa-calendar-alt"></i> الجدولة</h1>
      <div class="glass-card">
        <h3>إضافة تنبيه</h3>
        <label>ملف الصوت</label><select id="scheduleFile"></select>
        <label>النوع</label>
        <select id="scheduleType" onchange="toggleScheduleFields()">
          <option value="daily">يومي</option><option value="weekly">أسبوعي</option>
          <option value="monthly">شهري</option><option value="specific">تاريخ محدد</option>
        </select>
        <div id="scheduleExtraFields"></div>
        <label>الوقت</label><input type="time" id="scheduleTime">
        <button class="btn" onclick="addSchedule()">حفظ</button>
      </div>
      <div class="glass-card"><h3>التنبيهات المجدولة</h3><ul id="scheduleList"></ul></div>
    </div>

    <!-- GPIO -->
    <div id="tab-gpio" class="tab">
      <h1><i class="fas fa-microchip"></i> GPIO</h1>
      <div class="glass-card">
        <h3>ربط مدخل</h3>
        <label>رقم المدخل</label><select id="inputPin"><option>14</option></select>
        <label>الملف</label><select id="inputFile"></select>
        <button class="btn" onclick="saveInputMapping()">حفظ</button>
      </div>
      <div class="glass-card">
        <h3>ربط تنبيه بمخرج</h3>
        <label>التنبيه</label><select id="alertForGPIO"></select>
        <label>رقم المخرج</label><select id="outputPin"><option>13</option></select>
        <label>المدة (ثواني)</label><input type="number" id="outputDuration" value="5">
        <button class="btn" onclick="saveOutputMapping()">حفظ</button>
      </div>
    </div>

    <!-- Eid -->
    <div id="tab-eid" class="tab">
      <h1><i class="fas fa-star-and-crescent"></i> وضع العيد</h1>
      <div class="glass-card">
        <label class="switch"><input type="checkbox" id="eidModeToggle" onchange="toggleEidMode()"><span class="slider"></span></label> تفعيل
        <br><br>
        <button class="btn" onclick="triggerTakbeer()">تشغيل التكبيرات</button>
      </div>
    </div>

    <!-- Player -->
    <div id="tab-player" class="tab">
      <h1><i class="fas fa-music"></i> مشغل الصوت</h1>
      <div class="glass-card">
        <label>اختر ملف</label><select id="musicFile"></select>
        <button class="btn" onclick="playMusic()"><i class="fas fa-play"></i></button>
        <button class="btn" onclick="pauseMusic()"><i class="fas fa-pause"></i></button>
        <button class="btn btn-danger" onclick="stopMusic()"><i class="fas fa-stop"></i></button>
        <label>مدة (دقائق، 0=كامل)</label><input type="number" id="musicDuration" value="0">
        <input type="range" id="musicVolume" min="0" max="30" value="15" onchange="adjustMusicVolume(this.value)">
      </div>
    </div>

    <!-- Maghrib -->
    <div id="tab-maghrib" class="tab">
      <h1><i class="fas fa-sun"></i> تنبيهات قبل المغرب</h1>
      <div class="glass-card">
        <p>حدد ملف لكل يوم، وسيبدأ التشغيل تلقائياً لينتهي قبل الأذان بدقيقة.</p>
        <div id="maghribAlerts"></div>
        <button class="btn" onclick="saveMaghribAlerts()">حفظ</button>
        <button class="btn" onclick="loadMaghribAlerts()">تحديث</button>
      </div>
    </div>
  </main>

  <script>
    // --- التنقل بين التبويبات ---
    function showTab(tabName) {
      document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
      document.getElementById(`tab-${tabName}`).classList.add('active');
      document.querySelectorAll('.sidebar a').forEach(a => a.classList.remove('active'));
      document.querySelector(`a[href="#${tabName}"]`).classList.add('active');
      if (window.innerWidth < 768) document.querySelector('.sidebar').classList.remove('open');
    }
    window.addEventListener('hashchange', () => {
      const hash = location.hash.substring(1) || 'dashboard';
      showTab(hash);
    });

    // --- الوقت والتاريخ ---
    function updateClock() {
      fetch('/api/time').then(r=>r.text()).then(t=> document.getElementById('timeDisplay').innerText = t);
      fetch('/api/date').then(r=>r.json()).then(d=> {
        document.getElementById('gregDate').innerText = d.greg;
        document.getElementById('hijriDate').innerText = d.hijri;
      });
    }
    setInterval(updateClock, 1000);
    updateClock();

    function fetchStatus() {
      fetch('/api/status').then(r=>r.json()).then(s=> {
        document.getElementById('playingStatus').innerText = s.playing ? 'يتم التشغيل: ' + s.file : 'متوقف';
        document.getElementById('volumeSlider').value = s.volume;
      });
    }
    setInterval(fetchStatus, 2000);

    function setVolume(v) { fetch(`/api/volume?level=${v}`); }
    function stopAudio() { fetch('/api/stop'); }
    function triggerAdhan(p) { fetch(`/api/adhan?prayer=${p}`); }
    function triggerIqama() { fetch('/api/iqama'); }

    // --- WiFi ---
    function scanWiFi() {
      fetch('/api/wifi/scan').then(r=>r.json()).then(nets=> {
        let html='';
        nets.forEach(n => html += `<div onclick="document.getElementById('ssid').value='${n.ssid}'">${n.ssid} (${n.rssi}dBm)</div>`);
        document.getElementById('wifiList').innerHTML = html;
      });
    }
    function toggleDHCP() {
      document.getElementById('staticIPFields').style.display = document.getElementById('dhcpToggle').checked ? 'none' : 'block';
    }
    function saveNetwork() {
      let data = { ssid: document.getElementById('ssid').value, pass: document.getElementById('wifiPass').value,
        dhcp: document.getElementById('dhcpToggle').checked,
        ip: document.getElementById('staticIP').value, gw: document.getElementById('gateway').value,
        mask: document.getElementById('subnet').value, dns: document.getElementById('dns').value };
      fetch('/api/wifi/save', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data)});
      alert('تم الحفظ');
    }

    // --- Prayer ---
    function fetchPrayerTimes() {
      let c = document.getElementById('countrySelect').value;
      let city = document.getElementById('citySelect').value;
      let m = document.getElementById('methodSelect').value;
      fetch(`/api/prayer/fetch?country=${c}&city=${city}&method=${m}`).then(r=>r.json()).then(times => {
        document.getElementById('nextPrayer').innerText = `الفجر ${times.fajr} | الظهر ${times.dhuhr} | العصر ${times.asr} | المغرب ${times.maghrib} | العشاء ${times.isha}`;
      });
    }

    // --- Files ---
    function loadFileList() {
      fetch('/api/files/list').then(r=>r.json()).then(files => {
        let html='';
        files.forEach(f => {
          html += `<div class="file-item"><span>${f.name} (${(f.size/1024/1024).toFixed(2)} MB)</span>
            <span><i class="fas fa-headphones" onclick="previewFile('${f.name}')" style="color:var(--primary);cursor:pointer;margin:0 8px"></i>
            <button class="btn" onclick="playFile('${f.name}')"><i class="fas fa-play"></i></button></span></div>`;
        });
        document.getElementById('fileList').innerHTML = html;
        populateSelects(files.map(f=>f.name));
      });
    }
    function uploadFile() {
      let f = document.getElementById('fileInput').files[0];
      if (!f) return;
      let form = new FormData(); form.append('file', f);
      let xhr = new XMLHttpRequest();
      xhr.open('POST', '/upload');
      xhr.upload.onprogress = e => {
        if (e.lengthComputable) {
          document.getElementById('uploadProgressBar').parentElement.style.display = 'block';
          document.getElementById('uploadProgressBar').style.width = (e.loaded/e.total*100)+'%';
        }
      };
      xhr.onload = () => { document.getElementById('uploadProgressBar').parentElement.style.display = 'none'; loadFileList(); };
      xhr.send(form);
    }
    function previewFile(name) {
      document.getElementById('previewCard').style.display = 'block';
      document.getElementById('previewName').innerText = name;
      document.getElementById('audioPlayer').src = `/api/files/stream?file=${encodeURIComponent(name)}`;
      document.getElementById('audioPlayer').play();
    }
    function closePreview() {
      document.getElementById('previewCard').style.display = 'none';
      document.getElementById('audioPlayer').pause();
    }
    function populateSelects(files) {
      ['scheduleFile','musicFile','inputFile','alertForGPIO'].forEach(id => {
        let sel = document.getElementById(id); if(!sel) return;
        sel.innerHTML = '<option value="">اختر</option>';
        files.forEach(f => { let o=document.createElement('option'); o.value=f; o.textContent=f; sel.appendChild(o); });
      });
      // adhanFileSelect also
      let adSel = document.getElementById('adhanFileSelect');
      if(adSel) { adSel.innerHTML = '<option value="">اختر</option>'; files.forEach(f => { let o=document.createElement('option'); o.value=f; o.textContent=f; adSel.appendChild(o); }); }
    }
    function playFile(name) { fetch(`/api/player/play?file=${encodeURIComponent(name)}&duration=0`); }
    loadFileList();

    // --- Scheduler ---
    function toggleScheduleFields() {
      let type = document.getElementById('scheduleType').value;
      let div = document.getElementById('scheduleExtraFields');
      if(type==='weekly') div.innerHTML='<label>يوم الأسبوع</label><select id="weekDay"><option value="0">الأحد</option><option value="1">الإثنين</option><option value="2">الثلاثاء</option><option value="3">الأربعاء</option><option value="4">الخميس</option><option value="5">الجمعة</option><option value="6">السبت</option></select>';
      else if(type==='monthly') div.innerHTML='<label>يوم الشهر</label><input type="number" id="monthDay" min="1" max="31" value="1">';
      else if(type==='specific') div.innerHTML='<label>التاريخ</label><input type="date" id="specificDate">';
      else div.innerHTML='';
    }
    function addSchedule() {
      let file = document.getElementById('scheduleFile').value;
      let type = document.getElementById('scheduleType').value;
      let time = document.getElementById('scheduleTime').value.split(':');
      let data = { file, type, hour: parseInt(time[0]), minute: parseInt(time[1]), enabled: true };
      if(type==='weekly') data.dayOfWeek = parseInt(document.getElementById('weekDay').value);
      else if(type==='monthly') data.dayOfMonth = parseInt(document.getElementById('monthDay').value);
      else if(type==='specific') data.specificDate = document.getElementById('specificDate').value;
      fetch('/api/schedule/add', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data)})
        .then(() => loadSchedules());
    }
    function loadSchedules() {
      fetch('/api/schedule/list').then(r=>r.json()).then(arr => {
        let html='';
        arr.forEach((a,i) => {
          html += `<li>${a.file} (${a.type} ${a.hour}:${a.minute}) <button onclick="deleteSchedule(${i})" class="btn btn-danger"><i class="fas fa-trash"></i></button></li>`;
        });
        document.getElementById('scheduleList').innerHTML = html;
      });
    }
    function deleteSchedule(i) { fetch(`/api/schedule/remove?index=${i}`).then(()=>loadSchedules()); }
    loadSchedules();

    // --- GPIO ---
    function saveInputMapping() {
      let pin = document.getElementById('inputPin').value;
      let file = document.getElementById('inputFile').value;
      fetch('/api/gpio/input', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({pin, file})});
    }
    function saveOutputMapping() {
      let pin = document.getElementById('outputPin').value;
      let alert = document.getElementById('alertForGPIO').value;
      let duration = document.getElementById('outputDuration').value;
      fetch('/api/gpio/output', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({pin, alert, duration})});
    }

    // --- Eid ---
    function toggleEidMode() { fetch(`/api/eid/mode?enable=${document.getElementById('eidModeToggle').checked?1:0}`); }
    function triggerTakbeer() { fetch('/api/eid/takbeer'); }

    // --- Player ---
    function playMusic() {
      let file = document.getElementById('musicFile').value;
      let dur = document.getElementById('musicDuration').value;
      fetch(`/api/player/play?file=${encodeURIComponent(file)}&duration=${dur}`);
    }

    // --- Maghrib ---
    const days = ["الأحد","الإثنين","الثلاثاء","الأربعاء","الخميس","الجمعة","السبت"];
    function loadMaghribAlerts() {
      fetch('/api/maghrib/alerts').then(r=>r.json()).then(arr => {
        let html='<table style="width:100%"><tr><th>اليوم</th><th>الملف</th><th>المدة (ثانية)</th><th>تفعيل</th></tr>';
        arr.forEach((a,i) => {
          html += `<tr><td>${days[i]}</td><td><select class="maghribFile" data-day="${i}"><option value="">-- لا يوجد --</option></select></td>
            <td><span id="dur-${i}">${a.duration||0}</span></td>
            <td><label class="switch"><input type="checkbox" class="maghribEnable" data-day="${i}" ${a.enabled?'checked':''}><span class="slider"></span></label></td></tr>`;
        });
        html += '</table>';
        document.getElementById('maghribAlerts').innerHTML = html;
        fetch('/api/files/list').then(r=>r.json()).then(files => {
          document.querySelectorAll('.maghribFile').forEach(sel => {
            sel.innerHTML = '<option value="">-- لا يوجد --</option>';
            files.forEach(f => { let o = document.createElement('option'); o.value=f.name; o.textContent=f.name; sel.appendChild(o); });
          });
          arr.forEach((a,i) => { let sel = document.querySelector(`.maghribFile[data-day='${i}']`); if(sel && a.file) sel.value = a.file; });
        });
      });
    }
    function saveMaghribAlerts() {
      let alerts = [];
      document.querySelectorAll('.maghribFile').forEach(sel => {
        let day = sel.getAttribute('data-day');
        let file = sel.value;
        let enabled = document.querySelector(`.maghribEnable[data-day='${day}']`).checked;
        alerts.push({day: parseInt(day), file, enabled});
      });
      fetch('/api/maghrib/save', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({alerts})})
        .then(() => alert('تم الحفظ'));
    }
    window.addEventListener('load', () => { if(document.getElementById('maghribAlerts')) loadMaghribAlerts(); });
  </script>
</body>
</html>
)rawliteral";

#endif