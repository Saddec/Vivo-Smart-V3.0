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
  <title>Vivo Smart – لوحة التحكم</title>
  <link href="https://fonts.googleapis.com/css2?family=Tajawal:wght@300;400;500;700&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
  <style>
    :root {
      --primary-blue: #3498db;
      --primary-purple: #8e44ad;
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
    #loginOverlay {
      position: fixed; top: 0; left: 0; width: 100%; height: 100%;
      background: inherit; z-index: 9999;
      display: flex; align-items: center; justify-content: center;
    }
    .sidebar {
      width: 260px;
      background: rgba(20, 30, 50, 0.5);
      backdrop-filter: blur(20px);
      border-right: 1px solid var(--glass-border);
      padding: 30px 15px;
      display: flex; flex-direction: column; align-items: center;
      position: fixed; top: 0; bottom: 0; right: 0; z-index: 1000;
    }
    .sidebar .logo { font-size: 28px; font-weight: 700; margin-bottom: 40px; color: #fff; text-align: center; }
    .sidebar .logo i { font-size: 36px; display: block; }
    .sidebar a {
      display: flex; align-items: center; gap: 12px;
      color: rgba(255,255,255,0.8); text-decoration: none;
      padding: 14px 20px; border-radius: 15px; margin: 6px 0; width: 100%;
      font-size: 16px; font-weight: 500; transition: 0.3s; background: transparent;
    }
    .sidebar a:hover, .sidebar a.active { background: rgba(46,196,182,0.4); color: #fff; }
    .sidebar .version { margin-top: auto; font-size: 12px; color: rgba(255,255,255,0.5); }
    .main-content { flex: 1; margin-right: 260px; padding: 25px; transition: margin 0.3s; padding-bottom: 80px; }
    .glass-card {
      background: var(--glass-bg); backdrop-filter: blur(15px);
      border: 1px solid var(--glass-border); border-radius: var(--radius-xl);
      padding: 25px; margin-bottom: 25px; box-shadow: var(--shadow-light);
    }
    h1, h2, h3 { font-weight: 600; margin-bottom: 20px; }
    h1 { font-size: 26px; } h2 { font-size: 22px; } h3 { font-size: 20px; }
    .row { display: flex; flex-wrap: wrap; gap: 25px; margin-bottom: 20px; }
    .col { flex: 1; min-width: 280px; }
    .btn {
      display: inline-flex; align-items: center; gap: 8px;
      background: var(--btn-gradient); color: #fff; border: none;
      padding: 12px 28px; border-radius: var(--radius-full);
      cursor: pointer; font-size: 15px; font-weight: 600; transition: 0.3s;
      text-decoration: none; box-shadow: 0 8px 20px rgba(142, 68, 173, 0.4); margin: 5px;
    }
    .btn:hover { transform: translateY(-3px); box-shadow: 0 12px 25px rgba(142, 68, 173, 0.6); }
    .btn-danger { background: linear-gradient(135deg, #e74c3c, #c0392b); box-shadow: 0 8px 20px rgba(231,76,60,0.4); }
    input, select, textarea {
      width: 100%; padding: 14px 18px; margin: 10px 0 20px;
      border: 1px solid var(--glass-border); border-radius: var(--radius-xl);
      background: rgba(255,255,255,0.1); color: #fff; font-size: 15px; outline: none;
    }
    select {
      -webkit-appearance: none; -moz-appearance: none; appearance: none;
      background-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' viewBox='0 0 24 24' fill='white'><path d='M7 10l5 5 5-5z'/></svg>");
      background-repeat: no-repeat; background-position: left 10px center; background-size: 14px; padding-left: 40px;
    }
    label { color: rgba(255,255,255,0.85); font-size: 14px; font-weight: 500; }
    .file-list { max-height: 350px; overflow-y: auto; background: rgba(0,0,0,0.2); border-radius: var(--radius-xl); padding: 15px; }
    .file-item { display: flex; justify-content: space-between; align-items: center; padding: 12px 15px; margin-bottom: 8px; background: rgba(255,255,255,0.08); border-radius: 15px; }
    .tab { display: none; }
    .tab.active { display: block; }
    .switch { position: relative; display: inline-block; width: 50px; height: 26px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; background-color: rgba(255,255,255,0.3); border-radius: 34px; top: 0; left: 0; right: 0; bottom: 0; transition: 0.4s; }
    .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 3px; bottom: 3px; background: white; border-radius: 50%; transition: 0.4s; }
    input:checked + .slider { background-color: #8e44ad; }
    input:checked + .slider:before { transform: translateX(24px); }
    .footer {
      position: fixed; bottom: 0; left: 0; right: 0; text-align: center;
      padding: 12px; background: rgba(0,0,0,0.3); backdrop-filter: blur(15px);
      border-top: 1px solid rgba(255,255,255,0.2); font-size: 16px; z-index: 999;
    }
    .footer .name { font-weight: 700; background: linear-gradient(135deg, #f1c40f, #e67e22); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    @media (max-width: 768px) { .sidebar { transform: translateX(100%); } .sidebar.open { transform: translateX(0); } .main-content { margin-right: 0; } }
  </style>
</head>
<body>
  <div id="loginOverlay">
    <div class="glass-card" style="width:350px; text-align:center;">
      <h2><i class="fas fa-lock"></i> تسجيل الدخول</h2>
      <input type="password" id="loginPassword" placeholder="كلمة المرور" style="text-align:center;">
      <button class="btn" onclick="doLogin()" style="width:100%; margin-top:15px;">دخول</button>
      <p id="loginError" style="color:#e74c3c; margin-top:10px; display:none;">كلمة المرور غير صحيحة</p>
    </div>
  </div>

  <nav class="sidebar" id="sidebar">
    <div class="logo"><i class="fas fa-mosque"></i> Vivo Smart</div>
    <a href="#dashboard" class="active" onclick="showTab('dashboard')"><i class="fas fa-home"></i> الرئيسية</a>
    <a href="#network" onclick="showTab('network')"><i class="fas fa-wifi"></i> الشبكة</a>
    <a href="#prayer" onclick="showTab('prayer')"><i class="fas fa-clock"></i> الصلاة</a>
    <a href="#files" onclick="showTab('files')"><i class="fas fa-folder-open"></i> الملفات</a>
    <a href="#scheduler" onclick="showTab('scheduler')"><i class="fas fa-calendar-alt"></i> الجدولة</a>
    <a href="#gpio" onclick="showTab('gpio')"><i class="fas fa-microchip"></i> GPIO</a>
    <a href="#gpio_schedule" onclick="showTab('gpio_schedule')"><i class="fas fa-clock"></i> جدولة GPIO</a>
    <a href="#eid" onclick="showTab('eid')"><i class="fas fa-star-and-crescent"></i> العيد</a>
    <a href="#player" onclick="showTab('player')"><i class="fas fa-music"></i> المشغل</a>
    <a href="#maghrib" onclick="showTab('maghrib')"><i class="fas fa-sun"></i> المغرب</a>
    <a href="#manual" onclick="showTab('manual')"><i class="fas fa-edit"></i> ضبط يدوي</a>
    <a href="#csv" onclick="showTab('csv')"><i class="fas fa-file-csv"></i> CSV</a>
    <a href="#startup" onclick="showTab('startup')"><i class="fas fa-power-off"></i> بدء التشغيل</a>
    <a href="#password" onclick="showTab('password')"><i class="fas fa-key"></i> كلمة المرور</a>
    <span class="version">v3.0 ESP32-S3</span>
  </nav>

  <main class="main-content" id="mainContent" style="display:none;">
    <!-- Dashboard -->
    <div id="tab-dashboard" class="tab active">
      <h1><i class="fas fa-tachometer-alt"></i> لوحة التحكم</h1>
      <div class="row">
        <div class="col glass-card">
          <h3><i class="far fa-clock"></i> الوقت</h3>
          <p style="font-size:28px; font-weight:700"><span id="timeDisplay">--:--</span></p>
          <p><span id="hijriDate"></span> | <span id="gregDate"></span></p>
        </div>
        <div class="col glass-card">
          <h3><i class="fas fa-volume-up"></i> الحالة</h3>
          <p id="playingStatus">متوقف</p>
          <input type="range" id="volumeSlider" min="0" max="30" value="15" onchange="setVolume(this.value)" style="width:100%">
          <button class="btn" onclick="stopAudio()"><i class="fas fa-stop"></i> إيقاف</button>
        </div>
      </div>
      <div class="glass-card">
        <h3><i class="fas fa-mosque"></i> مواقيت الصلاة</h3>
        <div class="row" style="justify-content:center">
          <div style="padding:10px"><span>الفجر</span><br><span id="fajrTime">--:--</span></div>
          <div style="padding:10px"><span>الظهر</span><br><span id="dhuhrTime">--:--</span></div>
          <div style="padding:10px"><span>العصر</span><br><span id="asrTime">--:--</span></div>
          <div style="padding:10px"><span>المغرب</span><br><span id="maghribTime">--:--</span></div>
          <div style="padding:10px"><span>العشاء</span><br><span id="ishaTime">--:--</span></div>
        </div>
        <p id="nextPrayer" style="text-align:center; font-size:18px;"></p>
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
          <label>الدولة</label>
          <select id="countrySelect" onchange="onCountryChange()"><option value="">اختر الدولة</option></select>
          <label>المدينة</label>
          <select id="citySelect"><option value="">اختر المدينة</option></select>
          <label>طريقة الحساب</label>
          <select id="methodSelect"><option value="0">الهيئة المصرية</option><option value="1">رابطة العالم الإسلامي</option><option value="2">أم القرى</option></select>
          <button class="btn" onclick="fetchPrayerTimes()">حساب المواقيت</button>
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
      </div>
      <div class="glass-card">
        <h3>مدير الملفات</h3>
        <button class="btn" onclick="createFolder()"><i class="fas fa-folder-plus"></i> إنشاء مجلد</button>
        <input type="text" id="newFolderName" placeholder="اسم المجلد الجديد">
        <div class="file-list" id="fileList"></div>
      </div>
      <div class="glass-card" id="previewCard" style="display:none">
        <h3>معاينة: <span id="previewName"></span></h3>
        <audio id="audioPlayer" controls style="width:100%"></audio>
        <button class="btn" onclick="closePreview()">إغلاق</button>
      </div>
      <div class="glass-card">
        <h3>تعيين ملفات الأذان والإقامة</h3>
        <label>أذان الفجر</label><select id="fajrAdhanFileSelect"></select>
        <label>باقي الصلوات</label><select id="adhanFileSelect"></select>
        <label>الإقامة</label><select id="iqamaFileSelect"></select>
        <button class="btn" onclick="saveAdhanAssignments()">حفظ</button>
      </div>
    </div>
    <!-- Scheduler -->
    <div id="tab-scheduler" class="tab">
      <h1><i class="fas fa-calendar-alt"></i> الجدولة</h1>
      <div class="glass-card">
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
        <input type="range" id="scheduleVolume" min="0" max="30" value="20">
        <span id="scheduleVolumeValue">20</span>
        <label>التكرار</label>
        <select id="scheduleLoopToggle" onchange="toggleLoopFields()">
          <option value="no">بدون تكرار</option>
          <option value="yes">بتكرار</option>
        </select>
        <div id="loopFields" style="display:none">
          <label>مدة التكرار (دقائق)</label><input type="number" id="scheduleLoopDuration" value="0" min="0">
        </div>
        <button class="btn" onclick="addSchedule()">حفظ</button>
      </div>
      <div class="glass-card"><h3>التنبيهات</h3><ul id="scheduleList"></ul></div>
    </div>
    <!-- GPIO -->
    <div id="tab-gpio" class="tab">
      <h1><i class="fas fa-microchip"></i> GPIO</h1>
      <div class="glass-card">
        <h3>ربط مدخل</h3>
        <label>الدبوس</label><select id="inputPin"><option>14</option><option>15</option><option>16</option><option>17</option><option>18</option><option>19</option><option>21</option><option>22</option><option>23</option><option>25</option><option>26</option><option>27</option><option>32</option><option>33</option></select>
        <label>الملف</label><select id="inputFile"></select>
        <button class="btn" onclick="saveInputMapping()">حفظ</button>
      </div>
      <div class="glass-card">
        <h3>ربط مخرج</h3>
        <label>الدبوس</label><select id="outputPin"><option>13</option><option>14</option><option>15</option><option>16</option><option>17</option><option>18</option><option>19</option><option>21</option><option>22</option><option>23</option><option>25</option><option>26</option><option>27</option><option>32</option><option>33</option></select>
        <label>المدة (ثواني)</label><input type="number" id="outputDuration" value="5">
        <button class="btn" onclick="saveOutputMapping()">حفظ</button>
      </div>
    </div>
    <!-- GPIO Schedule -->
    <div id="tab-gpio_schedule" class="tab">
      <h1><i class="fas fa-clock"></i> جدولة المخارج</h1>
      <div class="glass-card">
        <label>الدبوس</label><select id="gpioSchedPin"></select>
        <label>وقت البدء</label><input type="time" id="gpioSchedStart">
        <label>وقت الانتهاء</label><input type="time" id="gpioSchedEnd">
        <button class="btn" onclick="addGpioSchedule()">حفظ</button>
      </div>
    </div>
    <!-- Eid -->
    <div id="tab-eid" class="tab">
      <h1><i class="fas fa-star-and-crescent"></i> وضع العيد</h1>
      <div class="glass-card">
        <label class="switch"><input type="checkbox" id="eidModeToggle" onchange="toggleEidMode()"><span class="slider"></span></label> تفعيل
        <br><br>
        <label>ملف التكبيرات</label><select id="eidTakbeerFile"></select>
        <button class="btn" onclick="triggerTakbeer()">تشغيل التكبيرات</button>
        <button class="btn" onclick="saveEidFile()">حفظ الملف</button>
      </div>
    </div>
    <!-- Player -->
    <div id="tab-player" class="tab">
      <h1><i class="fas fa-music"></i> مشغل الموسيقى</h1>
      <div class="glass-card">
        <div id="playlist"></div>
        <button class="btn" onclick="addToPlaylist()">إضافة ملف</button>
        <select id="playlistFileSelect"></select>
      </div>
      <div class="glass-card">
        <button class="btn" onclick="playPlaylist()"><i class="fas fa-play"></i> تشغيل الكل</button>
        <button class="btn" onclick="stopPlaylist()"><i class="fas fa-stop"></i> إيقاف</button>
        <button class="btn" onclick="clearPlaylist()">مسح القائمة</button>
        <label>مستوى الصوت</label><input type="range" id="playlistVolume" min="0" max="30" value="15">
        <label class="switch"><input type="checkbox" id="playlistAdhanRespect"><span class="slider"></span></label> احترام الأذان
      </div>
    </div>
    <!-- Maghrib -->
    <div id="tab-maghrib" class="tab">
      <h1><i class="fas fa-sun"></i> تنبيهات المغرب</h1>
      <div class="glass-card">
        <label>قبل المغرب بـ (دقائق)</label><input type="number" id="maghribOffset" value="1">
        <button class="btn" onclick="saveMaghribOffset()">حفظ</button>
      </div>
      <div class="glass-card">
        <div id="maghribAlerts"></div>
        <button class="btn" onclick="saveMaghribAlerts()">حفظ التنبيهات</button>
        <button class="btn" onclick="loadMaghribAlerts()">تحديث</button>
      </div>
    </div>
    <!-- Manual -->
    <div id="tab-manual" class="tab">
      <h1><i class="fas fa-edit"></i> ضبط يدوي</h1>
      <div class="glass-card">
        <label class="switch"><input type="checkbox" id="manualModeToggle" onchange="toggleManualMode()"><span class="slider"></span></label> الوضع اليدوي
        <br><br>
        <label>الفجر</label><input type="time" id="manFajr" value="04:30">
        <label>الظهر</label><input type="time" id="manDhuhr" value="12:00">
        <label>العصر</label><input type="time" id="manAsr" value="15:30">
        <label>المغرب</label><input type="time" id="manMaghrib" value="18:00">
        <label>العشاء</label><input type="time" id="manIsha" value="19:30">
        <button class="btn" onclick="saveManualPrayerTimes()">حفظ</button>
      </div>
      <div class="glass-card">
        <h3>تحديث OTA</h3>
        <input type="file" id="otaFile" accept=".bin">
        <button class="btn" onclick="startOTA()">رفع التحديث</button>
      </div>
    </div>
    <!-- CSV -->
    <div id="tab-csv" class="tab">
      <h1><i class="fas fa-file-csv"></i> CSV</h1>
      <div class="glass-card">
        <label>الشهر</label>
        <select id="csvMonthSelect">
          <option value="1">يناير</option><option value="2">فبراير</option><option value="3">مارس</option><option value="4">أبريل</option><option value="5">مايو</option><option value="6">يونيو</option><option value="7">يوليو</option><option value="8">أغسطس</option><option value="9">سبتمبر</option><option value="10">أكتوبر</option><option value="11">نوفمبر</option><option value="12">ديسمبر</option>
        </select>
        <input type="file" id="csvFileInput" accept=".csv">
        <button class="btn" onclick="uploadCSV()"><i class="fas fa-upload"></i> رفع</button>
      </div>
      <div class="glass-card">
        <label class="switch"><input type="checkbox" id="csvModeToggle" onchange="toggleCSVMode()"><span class="slider"></span></label> تفعيل CSV
      </div>
    </div>
    <!-- Startup -->
    <div id="tab-startup" class="tab">
      <h1><i class="fas fa-power-off"></i> بدء التشغيل</h1>
      <div class="glass-card">
        <label class="switch"><input type="checkbox" id="startupAlertEnabled" onchange="toggleStartupAlert()"><span class="slider"></span></label>
        <label>الملف</label><select id="startupFileSelect"></select>
        <button class="btn" onclick="saveStartupSettings()">حفظ</button>
      </div>
    </div>
    <!-- Password -->
    <div id="tab-password" class="tab">
      <h1><i class="fas fa-key"></i> كلمة المرور</h1>
      <div class="glass-card">
        <label>القديمة</label><input type="password" id="oldPassword">
        <label>الجديدة</label><input type="password" id="newPassword">
        <label>تأكيد الجديدة</label><input type="password" id="confirmPassword">
        <button class="btn" onclick="changePassword()">تغيير</button>
      </div>
    </div>
    <!-- Footer -->
    <div class="footer">
      <span>جميع الحقوق محفوظة © 2027 - تصميم المهندس</span>
      <span class="name">صديق عبد العظيم</span>
    </div>
  </main>

  <script>
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

    function setVolume(v) { fetch('/api/volume?level=' + v); }
    function stopAudio() { fetch('/api/stop'); }
    function triggerAdhan(p) { fetch('/api/adhan?prayer=' + p); }
    function triggerIqama() { fetch('/api/iqama'); }

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

    function toggleEidMode() { fetch('/api/eid/mode?enable=' + (document.getElementById('eidModeToggle').checked ? 1 : 0)); }
    function triggerTakbeer() { fetch('/api/eid/takbeer'); }
    function saveEidFile() { fetch('/api/eid/file', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ file: document.getElementById('eidTakbeerFile').value }) }); }

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
  </script>
</body>
</html>
)rawliteral";

#endif