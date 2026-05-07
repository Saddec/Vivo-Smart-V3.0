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
  <link href="https://fonts.googleapis.com/css2?family=Tajawal:wght@300;400;500;700&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
  <style>
    :root {
      --primary-blue: #3498db;
      --primary-purple: #8e44ad;
      --deep-purple: #2c1340;
      --glass-bg: rgba(255, 255, 255, 0.15);
      --glass-border: rgba(255, 255, 255, 0.2);
      --btn-gradient: linear-gradient(135deg, #8e44ad, #3498db);
      --text-color: #ffffff;
      --shadow-light: 0 10px 30px rgba(0, 0, 0, 0.2);
      --radius-xl: 20px;
      --radius-full: 50px;
    }
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Tajawal', sans-serif;
      background: linear-gradient(180deg, #3498db 0%, #8e44ad 50%, #2c1340 100%);
      background-attachment: fixed;
      color: var(--text-color);
      display: flex;
      min-height: 100vh;
      overflow-x: hidden;
    }
    .sidebar {
      width: 260px;
      background: rgba(255, 255, 255, 0.1);
      backdrop-filter: blur(25px);
      -webkit-backdrop-filter: blur(25px);
      border-right: 1px solid var(--glass-border);
      padding: 30px 15px;
      display: flex;
      flex-direction: column;
      align-items: center;
      position: fixed;
      top: 0; bottom: 0; right: 0;
      z-index: 1000;
      box-shadow: 5px 0 25px rgba(0,0,0,0.1);
    }
    .sidebar .logo { font-size: 28px; font-weight: 700; margin-bottom: 40px; color: #fff; text-align: center; letter-spacing: 1px; text-shadow: 0 2px 5px rgba(0,0,0,0.2); }
    .sidebar .logo i { font-size: 36px; display: block; margin-bottom: 5px; }
    .sidebar a {
      display: flex; align-items: center; gap: 12px;
      color: rgba(255, 255, 255, 0.8); text-decoration: none;
      padding: 14px 20px; border-radius: var(--radius-xl);
      margin: 6px 0; width: 100%; font-size: 16px; font-weight: 500;
      transition: all 0.3s ease; background: transparent;
    }
    .sidebar a i { width: 22px; text-align: center; font-size: 18px; }
    .sidebar a:hover, .sidebar a.active { background: rgba(255, 255, 255, 0.25); color: #fff; transform: translateX(-5px); box-shadow: 0 5px 15px rgba(0,0,0,0.1); }
    .sidebar .version { margin-top: auto; font-size: 12px; color: rgba(255, 255, 255, 0.5); }
    .main-content { flex: 1; margin-right: 260px; padding: 25px; transition: margin 0.3s; }
    .mobile-menu-btn { display: none; position: fixed; top: 20px; right: 20px; z-index: 1100; background: var(--btn-gradient); border: none; color: #fff; font-size: 22px; width: 45px; height: 45px; border-radius: 50%; cursor: pointer; box-shadow: var(--shadow-light); }
    .glass-card { background: var(--glass-bg); backdrop-filter: blur(15px); -webkit-backdrop-filter: blur(15px); border: 1px solid var(--glass-border); border-radius: var(--radius-xl); padding: 25px; margin-bottom: 25px; box-shadow: var(--shadow-light); transition: all 0.3s ease; }
    .glass-card:hover { background: rgba(255, 255, 255, 0.2); border-color: rgba(255, 255, 255, 0.35); transform: translateY(-3px); }
    h1, h2, h3 { font-weight: 600; margin-bottom: 20px; text-shadow: 0 2px 5px rgba(0,0,0,0.15); }
    h1 { font-size: 26px; } h2 { font-size: 22px; } h3 { font-size: 20px; }
    .row { display: flex; flex-wrap: wrap; gap: 25px; margin-bottom: 20px; }
    .col { flex: 1; min-width: 280px; }
    .btn { display: inline-flex; align-items: center; justify-content: center; gap: 8px; background: var(--btn-gradient); color: #fff; border: none; padding: 12px 28px; border-radius: var(--radius-full); cursor: pointer; font-size: 15px; font-weight: 600; transition: all 0.3s ease; text-decoration: none; box-shadow: 0 8px 20px rgba(142, 68, 173, 0.4); margin: 5px; }
    .btn:hover { transform: translateY(-3px); box-shadow: 0 12px 25px rgba(142, 68, 173, 0.6); background: linear-gradient(135deg, #9b59b6, #3498db); }
    .btn-outline { background: transparent; border: 2px solid rgba(255, 255, 255, 0.5); box-shadow: none; }
    .btn-outline:hover { background: rgba(255, 255, 255, 0.15); border-color: #fff; box-shadow: 0 5px 15px rgba(0,0,0,0.2); }
    .btn-danger { background: linear-gradient(135deg, #e74c3c, #c0392b); box-shadow: 0 8px 20px rgba(231, 76, 60, 0.4); }
    input, select, textarea { width: 100%; padding: 14px 18px; margin: 10px 0 20px; border: 1px solid var(--glass-border); border-radius: var(--radius-xl); background: rgba(255, 255, 255, 0.1); color: #fff; font-size: 15px; outline: none; transition: all 0.3s ease; }
    input:focus, select:focus, textarea:focus { border-color: #fff; background: rgba(255, 255, 255, 0.2); box-shadow: 0 0 15px rgba(255,255,255,0.1); }
    label { color: rgba(255, 255, 255, 0.85); font-size: 14px; font-weight: 500; }
    .file-list { max-height: 350px; overflow-y: auto; background: rgba(0, 0, 0, 0.2); border-radius: var(--radius-xl); padding: 15px; }
    .file-item { display: flex; justify-content: space-between; align-items: center; padding: 12px 15px; margin-bottom: 8px; background: rgba(255, 255, 255, 0.08); border-radius: 15px; transition: all 0.3s ease; }
    .file-item:hover { background: rgba(255, 255, 255, 0.18); }
    .tab { display: none; }
    .tab.active { display: block; }
    .switch { position: relative; display: inline-block; width: 50px; height: 26px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; background-color: rgba(255,255,255,0.3); border-radius: 34px; top: 0; left: 0; right: 0; bottom: 0; transition: 0.4s; }
    .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 3px; bottom: 3px; background: white; border-radius: 50%; transition: 0.4s; }
    input:checked + .slider { background-color: #8e44ad; }
    input:checked + .slider:before { transform: translateX(24px); }
    @media (max-width: 768px) {
      .sidebar { transform: translateX(100%); }
      .sidebar.open { transform: translateX(0); }
      .main-content { margin-right: 0; }
      .mobile-menu-btn { display: block; }
    }
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
    <a href="#prayer" onclick="showTab('prayer')"><i class="fas fa-clock"></i> الصلاة</a>
    <a href="#files" onclick="showTab('files')"><i class="fas fa-folder-open"></i> الملفات</a>
    <a href="#scheduler" onclick="showTab('scheduler')"><i class="fas fa-calendar-alt"></i> الجدولة</a>
    <a href="#gpio" onclick="showTab('gpio')"><i class="fas fa-microchip"></i> GPIO</a>
    <a href="#eid" onclick="showTab('eid')"><i class="fas fa-star-and-crescent"></i> العيد</a>
    <a href="#player" onclick="showTab('player')"><i class="fas fa-music"></i> المشغل</a>
    <a href="#maghrib" onclick="showTab('maghrib')"><i class="fas fa-sun"></i> المغرب</a>
    <a href="#manual" onclick="showTab('manual')"><i class="fas fa-edit"></i> ضبط يدوي</a>
    <a href="#csv" onclick="showTab('csv')"><i class="fas fa-file-csv"></i> CSV</a>
    <a href="#startup" onclick="showTab('startup')"><i class="fas fa-power-off"></i> بدء التشغيل</a>
    <span class="version">v3.0 ESP32-S3</span>
  </nav>
  <main class="main-content" id="mainContent">
    <!-- Dashboard -->
    <div id="tab-dashboard" class="tab active">
      <h1><i class="fas fa-tachometer-alt"></i> لوحة التحكم</h1>
      <div class="row">
        <div class="col glass-card">
          <h3><i class="far fa-clock"></i> الوقت</h3>
          <p style="font-size:32px; font-weight:700"><span id="timeDisplay">--:--</span></p>
          <p><span id="hijriDate"></span> | <span id="gregDate"></span></p>
        </div>
        <div class="col glass-card">
          <h3><i class="fas fa-volume-up"></i> الحالة</h3>
          <p id="playingStatus">متوقف</p>
          <input type="range" id="volumeSlider" min="0" max="30" value="15" onchange="setVolume(this.value)" style="width:100%; accent-color:#fff;">
          <button class="btn" onclick="stopAudio()"><i class="fas fa-stop"></i> إيقاف</button>
        </div>
      </div>
      <div class="glass-card">
        <h3><i class="fas fa-mosque"></i> الأذان القادم</h3>
        <p id="nextPrayer">--:--</p>
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
        <label>اسم الشبكة</label><input type="text" id="ssid" placeholder="SSID">
        <label>كلمة المرور</label><input type="password" id="wifiPass" placeholder="كلمة السر">
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
          <label>الدولة</label>
          <select id="countrySelect" onchange="onCountryChange()">
            <option value="">اختر الدولة</option>
          </select>
          <label>المدينة</label>
          <select id="citySelect">
            <option value="">اختر المدينة</option>
          </select>
          <label>طريقة الحساب</label>
          <select id="methodSelect">
            <option value="0">الهيئة المصرية</option>
            <option value="1">رابطة العالم الإسلامي</option>
            <option value="2">أم القرى</option>
          </select>
          <button class="btn" onclick="fetchPrayerTimes()">جلب المواقيت</button>
        </div>
        <div class="col glass-card">
          <h3>الإزاحات (دقائق)</h3>
          <label>الفجر</label><input type="number" id="offsetFajr" value="0">
          <label>الظهر</label><input type="number" id="offsetDhuhr" value="0">
          <label>العصر</label><input type="number" id="offsetAsr" value="0">
          <label>المغرب</label><input type="number" id="offsetMaghrib" value="0">
          <label>العشاء</label><input type="number" id="offsetIsha" value="0">
          <button class="btn" onclick="saveOffsets()">حفظ الإزاحات</button>
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
        <div class="progress" style="display:none"><div id="uploadProgressBar" style="width:0%;background:linear-gradient(135deg,#8e44ad,#3498db);height:8px;border-radius:4px;"></div></div>
      </div>
      <div class="glass-card">
        <h3>مدير الملفات</h3>
        <div class="row">
          <input type="text" id="newFolderName" placeholder="اسم المجلد الجديد" style="flex:2;">
          <button class="btn" onclick="createFolder()"><i class="fas fa-folder-plus"></i> إنشاء مجلد</button>
        </div>
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
          <option value="prayer_relative">مرتبط بالصلاة</option>
        </select>
        <div id="scheduleExtraFields"></div>
        <label>الوقت</label><input type="time" id="scheduleTime">
        <label>مستوى الصوت (0-30)</label>
        <input type="range" id="scheduleVolume" min="0" max="30" value="20" oninput="document.getElementById('scheduleVolumeValue').innerText=this.value">
        <span id="scheduleVolumeValue">20</span>
        <label>التكرار (0 = بدون) دقائق</label>
        <input type="number" id="scheduleLoop" value="0" min="0">
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
        <input type="range" id="musicVolume" min="0" max="30" value="15" onchange="adjustMusicVolume(this.value)" style="accent-color:#fff;">
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

    <!-- Manual -->
    <div id="tab-manual" class="tab">
      <h1><i class="fas fa-edit"></i> ضبط يدوي للمواقيت والوقت</h1>
      <div class="glass-card">
        <h3>تفعيل الوضع اليدوي</h3>
        <label class="switch"><input type="checkbox" id="manualModeToggle" onchange="toggleManualMode()"><span class="slider"></span></label>
        <span id="manualModeLabel">استخدام الأوقات المدخلة يدوياً</span>
      </div>
      <div class="glass-card">
        <h3>أوقات الصلاة</h3>
        <label>الفجر</label><input type="time" id="manFajr" value="04:30">
        <label>الشروق</label><input type="time" id="manSunrise" value="06:00">
        <label>الظهر</label><input type="time" id="manDhuhr" value="12:00">
        <label>العصر</label><input type="time" id="manAsr" value="15:30">
        <label>المغرب</label><input type="time" id="manMaghrib" value="18:00">
        <label>العشاء</label><input type="time" id="manIsha" value="19:30">
        <button class="btn" onclick="saveManualPrayerTimes()"><i class="fas fa-save"></i> حفظ الأوقات</button>
      </div>
      <div class="glass-card">
        <h3>ضبط التاريخ والوقت</h3>
        <label>التاريخ</label><input type="date" id="manDate">
        <label>الوقت</label><input type="time" id="manTime">
        <button class="btn" onclick="setManualDateTime()"><i class="fas fa-clock"></i> تعيين</button>
      </div>
      <div class="glass-card">
        <h3>تحديث النظام (OTA)</h3>
        <input type="file" id="otaFile" accept=".bin">
        <button class="btn" onclick="startOTA()">رفع التحديث</button>
        <div id="otaStatus"></div>
      </div>
    </div>

    <!-- CSV -->
    <div id="tab-csv" class="tab">
      <h1><i class="fas fa-file-csv"></i> رفع ملفات CSV</h1>
      <div class="glass-card">
        <h3>رفع ملف شهر</h3>
        <label>الشهر</label>
        <select id="csvMonthSelect">
          <option value="1">يناير</option><option value="2">فبراير</option><option value="3">مارس</option>
          <option value="4">أبريل</option><option value="5">مايو</option><option value="6">يونيو</option>
          <option value="7">يوليو</option><option value="8">أغسطس</option><option value="9">سبتمبر</option>
          <option value="10">أكتوبر</option><option value="11">نوفمبر</option><option value="12">ديسمبر</option>
        </select>
        <input type="file" id="csvFileInput" accept=".csv">
        <button class="btn" onclick="uploadCSV()"><i class="fas fa-upload"></i> رفع</button>
        <div id="csvUploadStatus"></div>
      </div>
      <div class="glass-card">
        <h3>تفعيل وضع CSV</h3>
        <label class="switch"><input type="checkbox" id="csvModeToggle" onchange="toggleCSVMode()"><span class="slider"></span></label>
        <span id="csvModeLabel">استخدام ملفات CSV للمواقيت</span>
      </div>
      <div class="glass-card">
        <h3>الشهور المحملة</h3>
        <div id="loadedMonthsList"></div>
        <button class="btn" onclick="loadLoadedMonths()">تحديث</button>
      </div>
    </div>

    <!-- Startup Alert -->
    <div id="tab-startup" class="tab">
      <h1><i class="fas fa-power-off"></i> تنبيه بدء التشغيل</h1>
      <div class="glass-card">
        <h3>تفعيل التنبيه</h3>
        <label class="switch">
          <input type="checkbox" id="startupAlertEnabled" onchange="toggleStartupAlert()">
          <span class="slider"></span>
        </label>
        <span id="startupAlertLabel">تشغيل ملف صوتي عند بدء تشغيل الجهاز</span>
      </div>
      <div class="glass-card">
        <h3>اختيار الملف</h3>
        <select id="startupFileSelect">
          <option value="">اختر ملف</option>
        </select>
        <button class="btn" onclick="saveStartupSettings()"><i class="fas fa-save"></i> حفظ</button>
        <div id="startupSaveStatus"></div>
      </div>
    </div>
  </main>

  <script>
    // التنقل بين التبويبات
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

    // الوقت والتاريخ
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

    // WiFi
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

    // الدول والمدن
    let allCountries = [];
    let citiesMap = {};

    function loadCountries() {
      fetch('/api/location/countries')
        .then(r => r.json())
        .then(data => {
          allCountries = data;
          let sel = document.getElementById('countrySelect');
          sel.innerHTML = '<option value="">اختر الدولة</option>';
          allCountries.forEach(c => {
            let o = document.createElement('option');
            o.value = c;
            o.textContent = c;
            sel.appendChild(o);
          });
        });
    }

    function onCountryChange() {
      let country = document.getElementById('countrySelect').value;
      let citySel = document.getElementById('citySelect');
      citySel.innerHTML = '<option value="">اختر المدينة</option>';
      if (!country) return;
      fetch('/api/location/cities?country=' + encodeURIComponent(country))
        .then(r => r.json())
        .then(cities => {
          citiesMap[country] = cities;
          cities.forEach(city => {
            let o = document.createElement('option');
            o.value = city;
            o.textContent = city;
            citySel.appendChild(o);
          });
        });
    }

    // جلب المواقيت بعد اختيار الدولة والمدينة
    function fetchPrayerTimes() {
      let country = document.getElementById('countrySelect').value;
      let city = document.getElementById('citySelect').value;
      let method = document.getElementById('methodSelect').value;
      if (!country || !city) return alert('اختر الدولة والمدينة');
      fetch(`/api/prayer/fetch?country=${encodeURIComponent(country)}&city=${encodeURIComponent(city)}&method=${method}`)
        .then(r => r.json())
        .then(times => {
          document.getElementById('nextPrayer').innerText = `الفجر ${times.fajr} | الظهر ${times.dhuhr} | العصر ${times.asr} | المغرب ${times.maghrib} | العشاء ${times.isha}`;
        });
    }

    // تحميل الدول عند فتح الصفحة
    window.addEventListener('load', () => {
      if (document.getElementById('countrySelect')) loadCountries();
    });

    // ملفات (متطورة)
    let currentDir = '/';
    function loadFileList(dir = currentDir) {
      currentDir = dir;
      fetch('/api/files/list?dir=' + encodeURIComponent(dir))
      .then(r => r.json())
      .then(files => {
        let html = '';
        if (dir !== '/') {
          html += `<div class="file-item" onclick="loadFileList('/')" style="cursor:pointer;"><i class="fas fa-arrow-up"></i> ... (العودة للجذر)</div>`;
        }
        files.forEach(f => {
          let icon = f.isDirectory ? '<i class="fas fa-folder"></i>' : '<i class="fas fa-file-audio"></i>';
          let sizeStr = f.isDirectory ? '' : (f.size/1024/1024).toFixed(2) + ' MB';
          html += `<div class="file-item">
            <span>${icon} ${f.name} ${sizeStr}</span>
            <span>
              ${!f.isDirectory ? `<i class="fas fa-headphones" onclick="previewFile('${dir}${f.name}')" style="color:#fff;cursor:pointer;margin:0 8px" title="معاينة"></i>` : ''}
              <button class="btn" onclick="${f.isDirectory ? `loadFileList('${dir}${f.name}/')` : `playFile('${dir}${f.name}')`}"><i class="fas fa-${f.isDirectory ? 'folder-open' : 'play'}"></i> ${f.isDirectory ? 'فتح' : 'تشغيل'}</button>
              <button class="btn btn-danger" onclick="deleteFile('${dir}${f.name}')"><i class="fas fa-trash"></i></button>
              <button class="btn btn-outline" onclick="renameFile('${dir}${f.name}')"><i class="fas fa-edit"></i></button>
            </span>
          </div>`;
        });
        document.getElementById('fileList').innerHTML = html;
        let allFiles = files.filter(f => !f.isDirectory).map(f => f.name);
        populateSelects(allFiles);
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
      xhr.onload = () => { document.getElementById('uploadProgressBar').parentElement.style.display = 'none'; loadFileList(currentDir); };
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
    function deleteFile(path) {
      if (!confirm('حذف ' + path + '؟')) return;
      fetch('/api/files/delete?file=' + encodeURIComponent(path), {method: 'DELETE'})
        .then(() => loadFileList(currentDir));
    }
    function renameFile(oldPath) {
      let newName = prompt('الاسم الجديد:', oldPath.split('/').pop());
      if (newName) {
        let newPath = oldPath.substring(0, oldPath.lastIndexOf('/')+1) + newName;
        fetch('/api/files/rename?old=' + encodeURIComponent(oldPath) + '&new=' + encodeURIComponent(newPath), {method: 'POST'})
          .then(() => loadFileList(currentDir));
      }
    }
    function createFolder() {
      let name = document.getElementById('newFolderName').value;
      if (!name) return;
      fetch('/api/files/mkdir?name=' + encodeURIComponent(name), {method: 'POST'})
        .then(() => { document.getElementById('newFolderName').value=''; loadFileList(currentDir); });
    }
    function populateSelects(files) {
      ['scheduleFile','musicFile','inputFile','alertForGPIO','startupFileSelect'].forEach(id => {
        let sel = document.getElementById(id); if(!sel) return;
        sel.innerHTML = '<option value="">اختر</option>';
        files.forEach(f => { let o=document.createElement('option'); o.value=f; o.textContent=f; sel.appendChild(o); });
      });
      let adSel = document.getElementById('adhanFileSelect');
      if(adSel) { adSel.innerHTML = '<option value="">اختر</option>'; files.forEach(f => { let o=document.createElement('option'); o.value=f; o.textContent=f; adSel.appendChild(o); }); }
    }
    function playFile(name) { fetch(`/api/player/play?file=${encodeURIComponent(name)}&duration=0`); }
    loadFileList('/');

    // الجدولة
    function toggleScheduleFields() {
      let type = document.getElementById('scheduleType').value;
      let div = document.getElementById('scheduleExtraFields');
      if(type==='weekly') div.innerHTML='<label>يوم الأسبوع</label><select id="weekDay"><option value="0">الأحد</option><option value="1">الإثنين</option><option value="2">الثلاثاء</option><option value="3">الأربعاء</option><option value="4">الخميس</option><option value="5">الجمعة</option><option value="6">السبت</option></select>';
      else if(type==='monthly') div.innerHTML='<label>يوم الشهر</label><input type="number" id="monthDay" min="1" max="31" value="1">';
      else if(type==='specific') div.innerHTML='<label>التاريخ</label><input type="date" id="specificDate">';
      else if(type==='prayer_relative') div.innerHTML=`
        <label>الصلاة</label>
        <select id="prayerSelect">
          <option value="0">الفجر</option><option value="1">الظهر</option><option value="2">العصر</option>
          <option value="3">المغرب</option><option value="4">العشاء</option>
        </select>
        <label>الإزاحة</label>
        <div style="display:flex; gap:10px">
          <input type="number" id="offsetValue" value="0" style="flex:2">
          <select id="offsetUnit" style="flex:1">
            <option value="1">ثواني</option><option value="60">دقائق</option><option value="3600">ساعات</option>
          </select>
        </div>
        <label>نوع الإزاحة</label>
        <select id="offsetDirection"><option value="1">بعد</option><option value="-1">قبل</option></select>
        <label>تاريخ البداية (اختياري)</label><input type="date" id="validFrom">
        <label>تاريخ النهاية (اختياري)</label><input type="date" id="validTo">
      `;
      else div.innerHTML='';
    }
    function addSchedule() {
      let file = document.getElementById('scheduleFile').value;
      let type = document.getElementById('scheduleType').value;
      let time = document.getElementById('scheduleTime').value.split(':');
      let data = { file, type, hour: parseInt(time[0]), minute: parseInt(time[1]), enabled: true };
      data.volume = parseInt(document.getElementById('scheduleVolume').value);
      // loop duration in seconds
      data.loop = parseInt(document.getElementById('scheduleLoop').value) * 60 || 0;
      if(type==='weekly') data.dayOfWeek = parseInt(document.getElementById('weekDay').value);
      else if(type==='monthly') data.dayOfMonth = parseInt(document.getElementById('monthDay').value);
      else if(type==='specific') data.specificDate = document.getElementById('specificDate').value;
      else if(type==='prayer_relative') {
        let offsetVal = parseInt(document.getElementById('offsetValue').value);
        let offsetUnit = parseInt(document.getElementById('offsetUnit').value);
        let direction = parseInt(document.getElementById('offsetDirection').value);
        data.isPrayerRelative = true;
        data.prayerIndex = parseInt(document.getElementById('prayerSelect').value);
        data.offsetSeconds = offsetVal * offsetUnit * direction;
        data.validFrom = document.getElementById('validFrom').value;
        data.validTo = document.getElementById('validTo').value;
      }
      fetch('/api/schedule/add', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data)})
        .then(() => loadSchedules());
    }
    function loadSchedules() {
      fetch('/api/schedule/list').then(r=>r.json()).then(arr => {
        let html='';
        arr.forEach((a,i) => {
          let desc = a.file + ' (' + a.type + ' ' + a.hour + ':' + String(a.minute).padStart(2,'0');
          if (a.isPrayerRelative) {
            const prayerNames = ['الفجر','الظهر','العصر','المغرب','العشاء'];
            let offsetSign = a.offsetSeconds >= 0 ? 'بعد ' : 'قبل ';
            let absOffset = Math.abs(a.offsetSeconds);
            let offsetStr = absOffset >= 3600 ? (absOffset/3600).toFixed(1)+' ساعة' : absOffset >= 60 ? (absOffset/60)+' دقيقة' : absOffset+' ثانية';
            desc = a.file + ' (مرتبط: ' + offsetSign + prayerNames[a.prayerIndex] + ' ' + offsetStr + ')';
            if (a.validFrom) desc += ' من ' + a.validFrom;
            if (a.validTo) desc += ' حتى ' + a.validTo;
          }
          desc += ' | مستوى الصوت: ' + (a.volume || 20) + ' | تكرار: ' + ((a.loop||0)/60) + ' دقيقة';
          html += `<li>${desc} <button onclick="deleteSchedule(${i})" class="btn btn-danger"><i class="fas fa-trash"></i></button></li>`;
        });
        document.getElementById('scheduleList').innerHTML = html;
      });
    }
    function deleteSchedule(i) { fetch(`/api/schedule/remove?index=${i}`).then(()=>loadSchedules()); }
    loadSchedules();

    // GPIO
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

    // Eid
    function toggleEidMode() { fetch(`/api/eid/mode?enable=${document.getElementById('eidModeToggle').checked?1:0}`); }
    function triggerTakbeer() { fetch('/api/eid/takbeer'); }

    // Player
    function playMusic() {
      let file = document.getElementById('musicFile').value;
      let dur = document.getElementById('musicDuration').value;
      fetch(`/api/player/play?file=${encodeURIComponent(file)}&duration=${dur}`);
    }
    function pauseMusic() { fetch('/api/pause'); }
    function stopMusic() { fetch('/api/stop'); }
    function adjustMusicVolume(v) { fetch(`/api/volume?level=${v}`); }

    // Maghrib (with volume & loop)
    const days = ["الأحد","الإثنين","الثلاثاء","الأربعاء","الخميس","الجمعة","السبت"];
    function loadMaghribAlerts() {
      fetch('/api/maghrib/alerts').then(r=>r.json()).then(arr => {
        let html='<table style="width:100%"><tr><th>اليوم</th><th>الملف</th><th>المدة (ث)</th><th>مستوى الصوت</th><th>التكرار (دقيقة)</th><th>تفعيل</th></tr>';
        arr.forEach((a,i) => {
          html += `<tr>
            <td>${days[i]}</td>
            <td><select class="maghribFile" data-day="${i}"><option value="">-- لا يوجد --</option></select></td>
            <td><span id="dur-${i}">${a.duration||0}</span></td>
            <td><input type="range" class="maghribVolume" data-day="${i}" min="0" max="30" value="${a.volume||15}" oninput="document.getElementById('vol-${i}').innerText=this.value"> <span id="vol-${i}">${a.volume||15}</span></td>
            <td><input type="number" class="maghribLoop" data-day="${i}" value="${(a.loop||0)/60}" min="0" style="width:60px;"> دقائق</td>
            <td><label class="switch"><input type="checkbox" class="maghribEnable" data-day="${i}" ${a.enabled?'checked':''}><span class="slider"></span></label></td>
          </tr>`;
        });
        html += '</table>';
        document.getElementById('maghribAlerts').innerHTML = html;
        fetch('/api/files/list').then(r=>r.json()).then(files => {
          document.querySelectorAll('.maghribFile').forEach(sel => {
            sel.innerHTML = '<option value="">-- لا يوجد --</option>';
            files.forEach(f => { if(!f.isDirectory) { let o = document.createElement('option'); o.value=f.name; o.textContent=f.name; sel.appendChild(o); } });
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
        let volume = parseInt(document.querySelector(`.maghribVolume[data-day='${day}']`).value);
        let loopMin = parseInt(document.querySelector(`.maghribLoop[data-day='${day}']`).value) || 0;
        alerts.push({day: parseInt(day), file, enabled, volume, loop: loopMin * 60});
      });
      fetch('/api/maghrib/save', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({alerts})})
        .then(() => alert('تم الحفظ'));
    }
    window.addEventListener('load', () => { if(document.getElementById('maghribAlerts')) loadMaghribAlerts(); });

    // Manual
    function loadManualSettings() {
      fetch('/api/prayer/manual/status').then(r=>r.json()).then(data=>{
        document.getElementById('manualModeToggle').checked = data.enabled;
        document.getElementById('manFajr').value = data.times.fajr || "04:30";
        document.getElementById('manDhuhr').value = data.times.dhuhr || "12:00";
        document.getElementById('manAsr').value = data.times.asr || "15:30";
        document.getElementById('manMaghrib').value = data.times.maghrib || "18:00";
        document.getElementById('manIsha').value = data.times.isha || "19:30";
        document.getElementById('manSunrise').value = data.times.sunrise || "06:00";
      });
    }
    function toggleManualMode() {
      let en = document.getElementById('manualModeToggle').checked;
      fetch('/api/prayer/manual/toggle', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({enabled:en}) })
        .then(() => { document.getElementById('manualModeLabel').innerText = en ? 'الوضع اليدوي مفعل' : 'الوضع التلقائي'; });
    }
    function saveManualPrayerTimes() {
      let times = {
        fajr: document.getElementById('manFajr').value,
        dhuhr: document.getElementById('manDhuhr').value,
        asr: document.getElementById('manAsr').value,
        maghrib: document.getElementById('manMaghrib').value,
        isha: document.getElementById('manIsha').value,
        sunrise: document.getElementById('manSunrise').value
      };
      fetch('/api/prayer/manual/save', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({times:times}) })
        .then(r=>r.text()).then(msg=>alert(msg));
    }
    function setManualDateTime() {
      let date = document.getElementById('manDate').value;
      let time = document.getElementById('manTime').value;
      if(!date || !time) return;
      let datetime = date + 'T' + time + ':00';
      fetch('/api/time/set', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({datetime:datetime}) })
        .then(r=>r.text()).then(msg=>alert(msg));
    }

    // OTA
    function startOTA() {
      let file = document.getElementById('otaFile').files[0];
      if(!file) { alert('اختر ملف .bin'); return; }
      let form = new FormData(); form.append('update', file);
      fetch('/update', { method:'POST', body:form })
        .then(r=>r.text()).then(msg=>{ document.getElementById('otaStatus').innerText = msg; setTimeout(location.reload,5000); });
    }

    // CSV
    function uploadCSV() {
      let month = document.getElementById('csvMonthSelect').value;
      let file = document.getElementById('csvFileInput').files[0];
      if(!file) return;
      let form = new FormData(); form.append('month', month); form.append('file', file);
      fetch('/api/csv/upload', { method:'POST', body:form })
        .then(r=>r.text()).then(msg=>{ document.getElementById('csvUploadStatus').innerHTML = '<div class="alert alert-success">'+msg+'</div>'; loadLoadedMonths(); });
    }
    function toggleCSVMode() {
      let en = document.getElementById('csvModeToggle').checked;
      fetch('/api/csv/mode/toggle', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({enabled:en}) })
        .then(() => { document.getElementById('csvModeLabel').innerText = en ? 'وضع CSV مفعل' : 'وضع CSV معطل'; });
    }
    function loadCSVStatus() {
      fetch('/api/csv/status').then(r=>r.json()).then(data=>{
        document.getElementById('csvModeToggle').checked = data.enabled;
        document.getElementById('csvModeLabel').innerText = data.enabled ? 'وضع CSV مفعل' : 'وضع CSV معطل';
        loadLoadedMonths();
      });
    }
    function loadLoadedMonths() {
      fetch('/api/csv/months').then(r=>r.json()).then(months=>{
        let html = '';
        const names = ["","يناير","فبراير","مارس","إبريل","مايو","يونيو","يوليو","أغسطس","سبتمبر","أكتوبر","نوفمبر","ديسمبر"];
        months.forEach(m=>{ html += `<span style="display:inline-block;margin:5px;padding:5px 10px;background:rgba(255,255,255,0.15);border-radius:15px;cursor:pointer;" onclick="deleteCSVMonth(${m})">${names[m]} <i class="fas fa-trash" style="font-size:0.7em;"></i></span>`; });
        document.getElementById('loadedMonthsList').innerHTML = html || '<span style="opacity:0.5;">لا توجد شهور محملة</span>';
      });
    }
    function deleteCSVMonth(month) { if(confirm('حذف شهر '+month+'؟')) fetch('/api/csv/delete?month='+month, {method:'DELETE'}).then(()=>loadLoadedMonths()); }

    // Startup Alert
    function loadStartupSettings() {
      fetch('/api/startup/status')
        .then(r => r.json())
        .then(data => {
          document.getElementById('startupAlertEnabled').checked = data.enabled;
          document.getElementById('startupAlertLabel').innerText = data.enabled ? 'مفعل' : 'معطل';
          fetch('/api/files/list?dir=/')
            .then(r => r.json())
            .then(files => {
              let sel = document.getElementById('startupFileSelect');
              sel.innerHTML = '<option value="">اختر ملف</option>';
              files.forEach(f => {
                if (!f.isDirectory) {
                  let o = document.createElement('option');
                  o.value = f.name;
                  o.textContent = f.name;
                  sel.appendChild(o);
                }
              });
              if (data.file) sel.value = data.file;
            });
        });
    }
    function toggleStartupAlert() {
      let en = document.getElementById('startupAlertEnabled').checked;
      document.getElementById('startupAlertLabel').innerText = en ? 'مفعل' : 'معطل';
    }
    function saveStartupSettings() {
      let enabled = document.getElementById('startupAlertEnabled').checked;
      let file = document.getElementById('startupFileSelect').value;
      fetch('/api/startup/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({enabled: enabled, file: file})
      })
      .then(r => r.text())
      .then(msg => {
        document.getElementById('startupSaveStatus').innerHTML = '<div class="alert alert-success">'+msg+'</div>';
      });
    }

    // تحميل الإعدادات عند فتح التبويبات المعنية
    window.addEventListener('load', function() {
      if (document.getElementById('manualModeToggle')) loadManualSettings();
      if (document.getElementById('csvModeToggle')) loadCSVStatus();
      if (document.getElementById('maghribAlerts')) loadMaghribAlerts();
      if (document.getElementById('startupFileSelect')) loadStartupSettings();
    });
  </script>
</body>
</html>
)rawliteral";

#endif