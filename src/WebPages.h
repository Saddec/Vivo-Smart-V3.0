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
      --ramadan-gold: #f1c40f;
      --ramadan-green: #2ecc71;
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
      position: relative;
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
    .main-content { flex: 1; margin-right: 260px; padding: 25px; transition: margin 0.3s; padding-bottom: 80px; }
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
    .btn-gold { background: linear-gradient(135deg, #f1c40f, #e67e22); color: #000; }
    input, select, textarea { width: 100%; padding: 14px 18px; margin: 10px 0 20px; border: 1px solid var(--glass-border); border-radius: var(--radius-xl); background: rgba(255, 255, 255, 0.1); color: #fff; font-size: 15px; outline: none; transition: all 0.3s ease; }
    select {
      -webkit-appearance: none;
      -moz-appearance: none;
      appearance: none;
      background-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' viewBox='0 0 24 24' fill='white'><path d='M7 10l5 5 5-5z'/></svg>");
      background-repeat: no-repeat;
      background-position: left 10px center;
      background-size: 14px;
      padding-left: 40px;
    }
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
    /* Ramadan decorations */
    .ramadan-lantern { position: absolute; top: 10px; left: 10px; font-size: 40px; opacity: 0.2; color: var(--ramadan-gold); animation: float 3s infinite; }
    .ramadan-crescent { position: absolute; bottom: 20px; right: 30px; font-size: 60px; opacity: 0.15; color: #fff; transform: rotate(-20deg); }
    @keyframes float { 0% { transform: translateY(0); } 50% { transform: translateY(-10px); } 100% { transform: translateY(0); } }
    .footer { position: fixed; bottom: 0; left: 0; right: 0; width: 100%; text-align: center; padding: 12px 20px; background: rgba(255,255,255,0.1); backdrop-filter: blur(15px); -webkit-backdrop-filter: blur(15px); border-top: 1px solid rgba(255,255,255,0.2); color: rgba(255,255,255,0.9); font-size: 16px; font-weight: 500; letter-spacing: 0.5px; z-index: 999; display: flex; align-items: center; justify-content: center; gap: 12px; }
    .footer i { color: #f1c40f; font-size: 14px; animation: pulse 2s infinite; }
    @keyframes pulse { 0% { opacity: 0.6; transform: scale(1); } 50% { opacity: 1; transform: scale(1.3); } 100% { opacity: 0.6; transform: scale(1); } }
    .footer .name { font-weight: 700; background: linear-gradient(135deg, #f1c40f, #e67e22); -webkit-background-clip: text; -webkit-text-fill-color: transparent; text-shadow: 0 0 10px rgba(241,196,15,0.3); }
    @media (max-width: 768px) {
      .sidebar { transform: translateX(100%); }
      .sidebar.open { transform: translateX(0); }
      .main-content { margin-right: 0; padding-bottom: 80px; }
      .mobile-menu-btn { display: block; }
    }
  </style>
</head>
<body>
  <div class="ramadan-lantern"><i class="fas fa-mosque"></i></div>
  <button class="mobile-menu-btn" onclick="document.querySelector('.sidebar').classList.toggle('open')">
    <i class="fas fa-bars"></i>
  </button>
  <nav class="sidebar">
    <div class="logo"><i class="fas fa-moon"></i> Vivo Smart</div>
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
    <span class="version">v3.0 ESP32-S3</span>
  </nav>
  <main class="main-content" id="mainContent">
    <!-- Dashboard -->
    <div id="tab-dashboard" class="tab active">
      <h1><i class="fas fa-tachometer-alt"></i> لوحة التحكم</h1>
      <div class="row">
        <div class="col glass-card">
          <h3><i class="far fa-clock"></i> الوقت والتاريخ</h3>
          <p style="font-size:28px; font-weight:700"><span id="timeDisplay">--:--</span></p>
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
        <h3><i class="fas fa-mosque"></i> مواقيت الصلاة اليوم</h3>
        <div class="row" style="justify-content: center;">
          <div class="prayer-time-box" id="fajrBox"><span>الفجر</span><br><span id="fajrTime">--:--</span></div>
          <div class="prayer-time-box" id="dhuhrBox"><span>الظهر</span><br><span id="dhuhrTime">--:--</span></div>
          <div class="prayer-time-box" id="asrBox"><span>العصر</span><br><span id="asrTime">--:--</span></div>
          <div class="prayer-time-box" id="maghribBox"><span>المغرب</span><br><span id="maghribTime">--:--</span></div>
          <div class="prayer-time-box" id="ishaBox"><span>العشاء</span><br><span id="ishaTime">--:--</span></div>
        </div>
        <p id="nextPrayer" style="text-align:center; font-size:18px; margin-top:10px;"></p>
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
        <h3>تعيين ملفات الأذان والإقامة</h3>
        <label>أذان الفجر</label>
        <select id="fajrAdhanFileSelect"><option>اختر ملف</option></select>
        <label>باقي الصلوات</label>
        <select id="adhanFileSelect"><option>اختر ملف</option></select>
        <label>الإقامة</label>
        <select id="iqamaFileSelect"><option>اختر ملف</option></select>
        <button class="btn" onclick="saveAdhanAssignments()">حفظ التعيينات</button>
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
        <label>التكرار</label>
        <select id="scheduleLoopToggle" onchange="toggleLoopFields()">
          <option value="no">بدون تكرار</option>
          <option value="yes">بتكرار</option>
        </select>
        <div id="loopFields" style="display:none">
          <label>مدة التكرار (بالدقائق)</label>
          <input type="number" id="scheduleLoopDuration" value="0" min="0">
        </div>
        <button class="btn" onclick="addSchedule()">حفظ</button>
      </div>
      <div class="glass-card"><h3>التنبيهات المجدولة</h3><ul id="scheduleList"></ul></div>
    </div>

    <!-- GPIO -->
    <div id="tab-gpio" class="tab">
      <h1><i class="fas fa-microchip"></i> GPIO</h1>
      <div class="glass-card">
        <h3>ربط مدخل</h3>
        <label>رقم المدخل</label>
        <select id="inputPin">
          <option>0</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option>
          <option>12</option><option>13</option><option>14</option><option>15</option><option>16</option><option>17</option>
          <option>18</option><option>19</option><option>21</option><option>22</option><option>23</option><option>25</option>
          <option>26</option><option>27</option><option>32</option><option>33</option>
        </select>
        <label>الملف</label><select id="inputFile"></select>
        <button class="btn" onclick="saveInputMapping()">حفظ</button>
      </div>
      <div class="glass-card">
        <h3>ربط مخرج</h3>
        <label>رقم المخرج</label>
        <select id="outputPin">
          <option>0</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option>
          <option>12</option><option>13</option><option>14</option><option>15</option><option>16</option><option>17</option>
          <option>18</option><option>19</option><option>21</option><option>22</option><option>23</option><option>25</option>
          <option>26</option><option>27</option><option>32</option><option>33</option>
        </select>
        <label>الملف المرتبط (اختياري)</label><select id="outputFile"><option value="">لا يوجد</option></select>
        <label>المدة (ثواني، 0=حتى إيقاف)</label><input type="number" id="outputDuration" value="5">
        <button class="btn" onclick="saveOutputMapping()">حفظ</button>
      </div>
    </div>

    <!-- GPIO Schedule -->
    <div id="tab-gpio_schedule" class="tab">
      <h1><i class="fas fa-clock"></i> جدولة المخارج</h1>
      <div class="glass-card">
        <h3>إضافة جدولة</h3>
        <label>رقم المخرج</label><select id="gpioSchedPin"></select>
        <label>النوع</label>
        <select id="gpioSchedType" onchange="toggleGpioSchedFields()">
          <option value="daily">يومي</option><option value="weekly">أسبوعي</option>
          <option value="monthly">شهري</option><option value="yearly">سنوي</option>
          <option value="specific">تاريخ محدد</option>
        </select>
        <div id="gpioSchedExtraFields"></div>
        <label>وقت البدء</label><input type="time" id="gpioSchedStart">
        <label>وقت الانتهاء</label><input type="time" id="gpioSchedEnd">
        <button class="btn" onclick="addGpioSchedule()">حفظ</button>
      </div>
    </div>

    <!-- Eid -->
    <div id="tab-eid" class="tab">
      <h1><i class="fas fa-star-and-crescent"></i> وضع العيد</h1>
      <div class="glass-card">
        <h3>تفعيل وضع العيد</h3>
        <label class="switch"><input type="checkbox" id="eidModeToggle" onchange="toggleEidMode()"><span class="slider"></span></label>
        <span id="eidModeLabel">استخدام الأوقات المدخلة يدوياً</span>
      </div>
      <div class="glass-card">
        <h3>ملف التكبيرات</h3>
        <select id="eidTakbeerFile"></select>
        <button class="btn" onclick="saveEidFile()">حفظ الملف</button>
      </div>
      <div class="glass-card">
        <h3>جدولة التكبيرات</h3>
        <label>النوع</label>
        <select id="eidSchedType" onchange="toggleEidSchedFields()">
          <option value="before_after">قبل/بعد الصلاة</option>
          <option value="custom">جدول مخصص</option>
          <option value="both">الاثنين</option>
        </select>
        <div id="eidSchedExtraFields"></div>
        <button class="btn" onclick="saveEidSchedule()">حفظ الجدولة</button>
      </div>
    </div>

    <!-- Player -->
    <div id="tab-player" class="tab">
      <h1><i class="fas fa-music"></i> مشغل الموسيقى المتقدم</h1>
      <div class="glass-card">
        <h3>قائمة التشغيل</h3>
        <div id="playlist"></div>
        <button class="btn" onclick="addToPlaylist()">إضافة ملف</button>
        <select id="playlistFileSelect"></select>
      </div>
      <div class="glass-card">
        <h3>التحكم</h3>
        <button class="btn" onclick="playPlaylist()"><i class="fas fa-play"></i> تشغيل الكل</button>
        <button class="btn" onclick="stopPlaylist()"><i class="fas fa-stop"></i> إيقاف</button>
        <button class="btn" onclick="clearPlaylist()">مسح القائمة</button>
      </div>
      <div class="glass-card">
        <h3>إعدادات الجدولة (اختياري)</h3>
        <label>نوع الجدولة</label>
        <select id="playlistSchedType" onchange="togglePlaylistSchedFields()">
          <option value="none">بدون</option>
          <option value="daily">يومي</option><option value="weekly">أسبوعي</option>
          <option value="specific">تاريخ محدد</option>
        </select>
        <div id="playlistSchedExtraFields"></div>
        <label>وقت البدء</label><input type="time" id="playlistStartTime">
        <label>مدة التشغيل (دقائق، 0=كامل)</label><input type="number" id="playlistDuration" value="0">
        <label>مستوى الصوت</label><input type="range" id="playlistVolume" min="0" max="30" value="15">
        <label class="switch"><input type="checkbox" id="playlistAdhanRespect"><span class="slider"></span></label> إيقاف مؤقت أثناء الأذان والإقامة
        <label>مدة التوقف بعد الأذان (ثواني)</label><input type="number" id="playlistAdhanPause" value="120">
        <button class="btn" onclick="savePlaylistSchedule()">حفظ الجدولة</button>
      </div>
    </div>

    <!-- Maghrib -->
    <div id="tab-maghrib" class="tab">
      <h1><i class="fas fa-sun"></i> تنبيهات قبل المغرب</h1>
      <div class="glass-card">
        <h3>الإعدادات العامة</h3>
        <label>وقت البدء قبل المغرب (بالدقائق)</label>
        <input type="number" id="maghribOffset" value="1" min="0">
        <button class="btn" onclick="saveMaghribOffset()">حفظ</button>
      </div>
      <div class="glass-card">
        <h3>الملفات اليومية</h3>
        <p>حدد ملف لكل يوم، وسيبدأ التشغيل تلقائياً لينتهي قبل الأذان بالوقت المحدد.</p>
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
        <label class="switch"><input type="checkbox" id="startupAlertEnabled" onchange="toggleStartupAlert()"><span class="slider"></span></label>
        <span id="startupAlertLabel">تشغيل ملف صوتي عند بدء تشغيل الجهاز</span>
      </div>
      <div class="glass-card">
        <h3>اختيار الملف</h3>
        <select id="startupFileSelect"><option value="">اختر ملف</option></select>
        <button class="btn" onclick="saveStartupSettings()"><i class="fas fa-save"></i> حفظ</button>
        <div id="startupSaveStatus"></div>
      </div>
    </div>

    <!-- Footer -->
    <div class="footer">
      <i class="fas fa-code"></i>
      <span>جميع الحقوق محفوظة © 2027 - تصميم المهندس</span>
      <span class="name">صديق عبد العظيم</span>
      <i class="fas fa-heart" style="color:#e74c3c;"></i>
    </div>
  </main>

  <script>
    // ... (جميع دوال JavaScript السابقة مع التعديلات التالية)

    // --- تحديث عرض الصلوات على الرئيسية ---
    function updatePrayerDisplay(times) {
      if (times) {
        document.getElementById('fajrTime').innerText = times.fajr || '--:--';
        document.getElementById('dhuhrTime').innerText = times.dhuhr || '--:--';
        document.getElementById('asrTime').innerText = times.asr || '--:--';
        document.getElementById('maghribTime').innerText = times.maghrib || '--:--';
        document.getElementById('ishaTime').innerText = times.isha || '--:--';
        // Highlight next prayer
        const prayers = ['fajr','dhuhr','asr','maghrib','isha'];
        const now = new Date();
        const currentMinutes = now.getHours() * 60 + now.getMinutes();
        let next = null;
        for (const p of prayers) {
          const t = times[p];
          if (!t) continue;
          const [h,m] = t.split(':').map(Number);
          const mins = h*60+m;
          if (mins > currentMinutes) { next = p; break; }
        }
        document.querySelectorAll('.prayer-time-box').forEach(b => b.classList.remove('next-prayer'));
        if (next) {
          const box = document.getElementById(next+'Box');
          if (box) box.classList.add('next-prayer');
          document.getElementById('nextPrayer').innerText = `الصلاة القادمة: ${next} - ${times[next]}`;
        }
      }
    }

    // --- تعديل toggleLoopFields ---
    function toggleLoopFields() {
      const val = document.getElementById('scheduleLoopToggle').value;
      document.getElementById('loopFields').style.display = val === 'yes' ? 'block' : 'none';
    }

    // --- تعديل addSchedule: إرسال loopDuration من الحقل الجديد ---
    function addSchedule() {
      // ... (نفس الكود السابق مع تعديل loop)
      let loopDuration = 0;
      if (document.getElementById('scheduleLoopToggle').value === 'yes') {
        loopDuration = parseInt(document.getElementById('scheduleLoopDuration').value) * 60 || 0;
      }
      data.loop = loopDuration;
      // ... الباقي
    }

    // --- حفظ تعيينات الأذان الجديدة ---
    function saveAdhanAssignments() {
      const fajrFile = document.getElementById('fajrAdhanFileSelect').value;
      const adhanFile = document.getElementById('adhanFileSelect').value;
      const iqamaFile = document.getElementById('iqamaFileSelect').value;
      fetch('/api/adhan/assign', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ fajr: fajrFile, adhan: adhanFile, iqama: iqamaFile })
      }).then(r => r.text()).then(msg => alert(msg));
    }

    // --- وظائف مشغل الموسيقى الجديدة ---
    let playlist = [];
    function addToPlaylist() {
      const file = document.getElementById('playlistFileSelect').value;
      if (!file) return;
      playlist.push(file);
      renderPlaylist();
    }
    function renderPlaylist() {
      let html = '';
      playlist.forEach((f,i) => {
        html += `<div class="file-item"><span>${f}</span> <button class="btn btn-danger" onclick="removeFromPlaylist(${i})"><i class="fas fa-trash"></i></button></div>`;
      });
      document.getElementById('playlist').innerHTML = html;
    }
    function removeFromPlaylist(index) { playlist.splice(index,1); renderPlaylist(); }
    function clearPlaylist() { playlist = []; renderPlaylist(); }
    function playPlaylist() {
      if (playlist.length === 0) return;
      // إرسال الأمر للخادم لتشغيل القائمة
      fetch('/api/player/playlist', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ files: playlist, volume: document.getElementById('playlistVolume').value })
      });
    }
    // ... (تحميل populateSelects وتعديلها لتشمل fajrAdhanFileSelect و iqamaFileSelect و playlistFileSelect)

    // --- دوال جدولة GPIO ---
    function toggleGpioSchedFields() {
      const type = document.getElementById('gpioSchedType').value;
      const div = document.getElementById('gpioSchedExtraFields');
      if (type === 'weekly') div.innerHTML = '...'; // اختيار يوم الأسبوع
      else if (type === 'monthly') div.innerHTML = '...';
      else if (type === 'specific') div.innerHTML = '<input type="date" id="gpioSchedDate">';
      else div.innerHTML = '';
    }

    // --- دوال العيد ---
    function toggleEidSchedFields() {
      const type = document.getElementById('eidSchedType').value;
      const div = document.getElementById('eidSchedExtraFields');
      if (type === 'before_after') {
        div.innerHTML = `
          <label>قبل الصلاة بـ (دقائق)</label><input type="number" id="eidBefore" value="15">
          <label>بعد الصلاة بـ (دقائق)</label><input type="number" id="eidAfter" value="15">
        `;
      } else if (type === 'custom') {
        div.innerHTML = '<label>أوقات مخصصة (HH:MM مفصولة بفاصلة)</label><input type="text" id="eidCustomTimes" placeholder="06:00,12:00,18:00">';
      } else if (type === 'both') {
        div.innerHTML = `
          <label>قبل الصلاة بـ (دقائق)</label><input type="number" id="eidBefore" value="15">
          <label>بعد الصلاة بـ (دقائق)</label><input type="number" id="eidAfter" value="15">
          <label>أوقات مخصصة إضافية</label><input type="text" id="eidCustomTimes" placeholder="06:00,12:00,18:00">
        `;
      } else div.innerHTML = '';
    }

    // دالة saveMaghribOffset للمغرب
    function saveMaghribOffset() {
      const offset = document.getElementById('maghribOffset').value;
      fetch('/api/maghrib/offset', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ offset: parseInt(offset) })
      }).then(r => r.text()).then(msg => alert(msg));
    }

    // ... (باقي الدوال السابقة تبقى كما هي مع تعديل populateSelects لتشمل fajrAdhanFileSelect, iqamaFileSelect, playlistFileSelect)
  </script>
</body>
</html>
)rawliteral";

#endif