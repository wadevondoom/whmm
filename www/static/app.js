const scanBtn = document.getElementById('scanBtn');
const scanStatus = document.getElementById('scanStatus');
const ssidSelect = document.getElementById('ssid');
const ssidManual = document.getElementById('ssidManual');
const form = document.getElementById('wifiForm');
const msg = document.getElementById('msg');

function setMsg(text) {
  msg.textContent = text || '';
}

async function doScan() {
  scanBtn.disabled = true;
  scanStatus.textContent = 'Scanning...';
  try {
    const res = await fetch('/scan');
    if (!res.ok) throw new Error('Scan failed');
    const list = await res.json();
    ssidSelect.innerHTML = '<option value="">-- choose or enter manually below --</option>';
    list.forEach(name => {
      const opt = document.createElement('option');
      opt.value = name;
      opt.textContent = name;
      ssidSelect.appendChild(opt);
    });
    scanStatus.textContent = `Found ${list.length} network(s)`;
  } catch (e) {
    scanStatus.textContent = 'Scan error';
  } finally {
    scanBtn.disabled = false;
  }
}

scanBtn?.addEventListener('click', () => doScan());

form?.addEventListener('submit', (e) => {
  // If manual SSID entered, prefer it
  if (ssidManual.value.trim()) {
    // Inject manual SSID by creating/setting a hidden input named 'ssid'
    const manual = ssidManual.value.trim();
    const hidden = document.createElement('input');
    hidden.type = 'hidden';
    hidden.name = 'ssid';
    hidden.value = manual;
    form.appendChild(hidden);
  }
  setMsg('Saving... device will reboot');
});
