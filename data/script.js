function showTab(tabName) {
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  let el = document.getElementById('tab-' + tabName);
  if (el) el.classList.add('active');
}

// WiFi scan
function scanWiFi() {
  fetch('/api/wifi/scan')
    .then(r => r.json())
    .then(data => {
      let html = '';
      data.networks.forEach(n => {
        html += `<div>${n.ssid} (${n.rssi}dBm)</div>`;
      });
      document.getElementById('wifiList').innerHTML = html;
    });
}

// File list
function loadFileList(dir = '/') {
  fetch('/api/files/list')
    .then(r => r.json())
    .then(data => {
      let files = data.files || [];
      let html = '';
      files.forEach(f => {
        html += `<div class="file-item">${f.name} (${(f.size/1024/1024).toFixed(2)} MB)</div>`;
      });
      document.getElementById('fileList').innerHTML = html;
    });
}