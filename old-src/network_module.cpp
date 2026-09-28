#include "app_shared.h"

namespace {
bool shouldShowViewer() {
  if (runtimeMode == RuntimeMode::ConnectedIdle && WiFi.status() == WL_CONNECTED) {
    return true;
  }

  if (runtimeMode == RuntimeMode::Recording &&
      recordingSession.modeBeforeRecording == RuntimeMode::ConnectedIdle &&
      WiFi.status() == WL_CONNECTED) {
    return true;
  }

  return false;
}

String buildSetupPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-CAM Setup</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #07111f;
      --panel: #101b2d;
      --panel-border: rgba(148, 163, 184, 0.2);
      --text: #e2e8f0;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --danger: #ef4444;
      --success: #22c55e;
      --field: #0b1220;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background:
        radial-gradient(circle at top left, rgba(56, 189, 248, 0.16), transparent 35%),
        linear-gradient(160deg, #050816, var(--bg));
      color: var(--text);
      padding: 18px;
    }
    .shell {
      max-width: 900px;
      margin: 0 auto;
      display: grid;
      gap: 16px;
    }
    .card {
      background: rgba(16, 27, 45, 0.95);
      border: 1px solid var(--panel-border);
      border-radius: 18px;
      padding: 18px;
      box-shadow: 0 24px 60px rgba(2, 6, 23, 0.35);
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 16px;
    }
    h1 {
      margin: 0 0 8px;
      font-size: clamp(28px, 5vw, 44px);
      letter-spacing: -0.04em;
    }
    h2 {
      margin: 0 0 12px;
      font-size: 18px;
    }
    p {
      margin: 0;
      color: var(--muted);
      line-height: 1.6;
    }
    .status, .error-box {
      padding: 12px 14px;
      border-radius: 12px;
      font-size: 14px;
      line-height: 1.5;
      border: 1px solid transparent;
    }
    .status {
      background: rgba(56, 189, 248, 0.12);
      border-color: rgba(56, 189, 248, 0.2);
      color: #dbeafe;
    }
    .status.success {
      background: rgba(34, 197, 94, 0.14);
      border-color: rgba(34, 197, 94, 0.22);
      color: #dcfce7;
    }
    .status.error, .error-box {
      background: rgba(239, 68, 68, 0.12);
      border-color: rgba(239, 68, 68, 0.22);
      color: #fee2e2;
    }
    .section-title {
      margin: 14px 0 8px;
      color: var(--muted);
      font-size: 12px;
      letter-spacing: 0.1em;
      text-transform: uppercase;
    }
    input, button {
      width: 100%;
      border-radius: 12px;
      border: 1px solid rgba(148, 163, 184, 0.22);
      background: var(--field);
      color: var(--text);
      font-size: 15px;
    }
    input {
      padding: 12px 14px;
      outline: none;
    }
    button {
      padding: 12px 14px;
      cursor: pointer;
      margin-top: 10px;
      font-weight: 700;
    }
    .primary {
      background: linear-gradient(135deg, var(--accent), #0ea5e9);
      border-color: transparent;
      color: #021018;
    }
    .secondary {
      background: rgba(15, 23, 42, 0.85);
    }
    .danger {
      background: rgba(239, 68, 68, 0.14);
      border-color: rgba(239, 68, 68, 0.25);
      color: #fecaca;
    }
    .toggle {
      display: flex;
      align-items: center;
      gap: 10px;
      color: var(--muted);
      font-size: 14px;
      margin-top: 10px;
    }
    .toggle input {
      width: auto;
    }
    .networks {
      display: grid;
      gap: 8px;
      max-height: 260px;
      overflow: auto;
      margin-top: 10px;
    }
    .network {
      text-align: left;
      background: rgba(2, 6, 23, 0.26);
      border: 1px solid rgba(148, 163, 184, 0.16);
      margin-top: 0;
    }
    .network span:last-child {
      float: right;
      color: var(--muted);
    }
    .meta {
      display: grid;
      gap: 10px;
      margin-top: 12px;
    }
    .meta-row {
      padding: 12px;
      border-radius: 12px;
      background: rgba(2, 6, 23, 0.26);
      border: 1px solid rgba(148, 163, 184, 0.16);
      font-size: 14px;
    }
    .meta-label {
      display: block;
      color: var(--muted);
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      margin-bottom: 4px;
    }
    @media (max-width: 860px) {
      .grid {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="shell">
    <div class="card">
      <h1>ESP32-CAM Setup</h1>
      <p>Connect to the setup AP, save Wi-Fi and gyro options here, and the firmware will apply them without leaving the page blind.</p>
    </div>

    <div id="status" class="status">Ready to configure Wi-Fi and optional gyro authentication.</div>
    <div id="lastError" class="error-box" style="display:none;"></div>

    <div class="grid">
      <div class="card">
        <h2>Wi-Fi</h2>
        <button class="secondary" type="button" onclick="scanNetworks()">Scan Networks</button>
        <div id="networks" class="networks"></div>

        <div class="section-title">SSID</div>
        <input id="ssid" type="text" value="__SAVED_SSID__" placeholder="Wi-Fi SSID" autocomplete="off" />

        <div class="section-title">Password</div>
        <input id="password" type="password" placeholder="Wi-Fi password" autocomplete="current-password" />
        <label class="toggle">
          <input id="showPassword" type="checkbox" onchange="togglePassword()" />
          Show password
        </label>
      </div>

      <div class="card">
        <h2>Gyro + Authentication</h2>
        <label class="toggle">
          <input id="gyroEnabled" type="checkbox" __GYRO_CHECKED__ />
          Enable gyro + authentication
        </label>
        <p id="gyroAvailabilityHint" style="margin-top:10px;">Checking gyro hardware availability...</p>

        <div class="section-title">Validation Route</div>
        <input id="validationRoute" type="text" value="__SAVED_ROUTE__" placeholder="https://your-backend" />

        <div class="section-title">Email (optional if UUID already exists)</div>
        <input id="email" type="text" placeholder="Email" autocomplete="username" />

        <div class="section-title">Password (optional if UUID already exists)</div>
        <input id="authPassword" type="password" placeholder="Password" autocomplete="current-password" />

        <button class="primary" type="button" onclick="saveAndConnect()">Save & Apply</button>
        <button class="danger" type="button" onclick="forgetWiFi()">Clear Saved Wi-Fi</button>
      </div>
    </div>

    <div class="card">
      <h2>Live Status</h2>
      <div class="meta">
        <div class="meta-row"><span class="meta-label">Mode</span><span id="modeValue">SoftApConfig</span></div>
        <div class="meta-row"><span class="meta-label">Connected IP</span><span id="stationIpValue">-</span></div>
        <div class="meta-row"><span class="meta-label">SD Card</span><span id="sdValue">Checking...</span></div>
        <div class="meta-row"><span class="meta-label">Time Sync</span><span id="timeValue">Not synced</span></div>
        <div class="meta-row"><span class="meta-label">Recording</span><span id="recordingValue">Idle</span></div>
      </div>
    </div>
  </div>

  <script>
    const $ = (id) => document.getElementById(id);

    function setStatus(message, kind = 'info') {
      const node = $('status');
      node.className = 'status' + (kind && kind !== 'info' ? ' ' + kind : '');
      node.textContent = message;
    }

    function togglePassword() {
      $('password').type = $('showPassword').checked ? 'text' : 'password';
    }

    function renderLastError(type, message) {
      const box = $('lastError');
      if (!message) {
        box.style.display = 'none';
        box.textContent = '';
        return;
      }
      box.style.display = 'block';
      box.textContent = 'Previous failure (' + type + '): ' + message;
    }

    function renderNetworks(networks) {
      const list = $('networks');
      list.innerHTML = '';
      networks.forEach((network) => {
        const item = document.createElement('button');
        item.type = 'button';
        item.className = 'network';
        item.innerHTML = '<span>' + network.ssid + '</span><span>' + network.rssi + ' dBm</span>';
        item.onclick = () => {
          $('ssid').value = network.ssid === '(hidden)' ? '' : network.ssid;
        };
        list.appendChild(item);
      });
    }

    function updateGyroAvailability(status) {
      const available = !!status.gyroAvailable;
      const enabled = $('gyroEnabled');
      const hint = $('gyroAvailabilityHint');
      const route = $('validationRoute');
      const email = $('email');
      const authPassword = $('authPassword');

      enabled.disabled = !available;
      route.disabled = !available;
      email.disabled = !available;
      authPassword.disabled = !available;

      if (!available) {
        enabled.checked = false;
        hint.textContent = 'Gyro hardware was not detected on GPIO3/GPIO13. Gyro verification is unavailable.';
      } else {
        hint.textContent = 'Gyro hardware detected. You can enable gyro verification if needed.';
      }
    }

    async function refreshStatus() {
      try {
        const response = await fetch('/status', { cache: 'no-store' });
        const status = await response.json();

        $('modeValue').textContent = status.mode;
        $('stationIpValue').textContent = status.stationIp || '-';
        $('sdValue').textContent = status.sdReady ? 'Ready' : 'Unavailable';
        $('timeValue').textContent = status.timeSynced ? 'Synced' : 'Not synced';
        $('recordingValue').textContent = status.recordingActive ? ('Writing to ' + (status.recordingDirectory || 'session')) : 'Idle';
        updateGyroAvailability(status);

        renderLastError(status.lastErrorType, status.lastErrorMessage);

        if (status.scanInProgress) {
          setStatus('Scanning for Wi-Fi networks...', 'info');
        } else if (status.scanError) {
          setStatus(status.scanError, 'error');
        } else if (Array.isArray(status.networks) && status.networks.length) {
          renderNetworks(status.networks);
        }

        if (status.mode === 'WifiConnecting') {
          setStatus('Connecting to Wi-Fi...', 'info');
        } else if (status.mode === 'PostConnectValidation') {
          setStatus('Wi-Fi connected. Holding success status before switching modes.', 'success');
        } else if (status.mode === 'ErrorFallback') {
          setStatus('Configuration failed. Returning to setup mode.', 'error');
        } else if (status.mode === 'ConnectedIdle') {
          setStatus('Connected to Wi-Fi. The viewer page is available at /.', 'success');
        } else if (status.mode === 'Recording') {
          setStatus('Recording is active. Click the record button again to stop.', 'success');
        }
      } catch (error) {
        setStatus('Unable to fetch device status.', 'error');
      }
    }

    async function scanNetworks() {
      try {
        const response = await fetch('/scan', { cache: 'no-store' });
        const result = await response.json();
        if (result.error) {
          setStatus(result.error, 'error');
          return;
        }
        if (result.inProgress) {
          setStatus('Scanning for Wi-Fi networks...', 'info');
        } else {
          renderNetworks(result.networks || []);
        }
      } catch (error) {
        setStatus('Wi-Fi scan request failed.', 'error');
      }
    }

    async function saveAndConnect() {
      const ssid = $('ssid').value.trim();
      const password = $('password').value;
      const gyroEnabled = $('gyroEnabled').checked ? '1' : '0';
      const validationRoute = $('validationRoute').value.trim();
      const email = $('email').value.trim();
      const authPassword = $('authPassword').value;

      if (!ssid) {
        setStatus('SSID is required.', 'error');
        return;
      }

      if (gyroEnabled === '1' && validationRoute.length === 0) {
        setStatus('Validation route is required when gyro is enabled.', 'error');
        return;
      }

      if ((email.length > 0 && authPassword.length === 0) || (email.length === 0 && authPassword.length > 0)) {
        setStatus('Enter both email and password, or leave both empty.', 'error');
        return;
      }

      setStatus('Applying configuration...', 'info');

      try {
        const response = await fetch('/connect', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({ ssid, password, gyroEnabled, validationRoute, email, authPassword })
        });
        const result = await response.json();
        if (!result.success) {
          setStatus(result.error || 'Unable to start configuration.', 'error');
          return;
        }

        setStatus('Configuration accepted. Waiting for the device to finish applying it.', 'info');
      } catch (error) {
        setStatus('Configuration request failed.', 'error');
      }
    }

    async function forgetWiFi() {
      try {
        const response = await fetch('/forget', { method: 'POST' });
        const result = await response.json();
        if (result.success) {
          $('ssid').value = '';
          $('password').value = '';
          setStatus('Saved Wi-Fi credentials cleared.', 'success');
          refreshStatus();
        } else {
          setStatus('Failed to clear saved Wi-Fi credentials.', 'error');
        }
      } catch (error) {
        setStatus('Failed to clear saved Wi-Fi credentials.', 'error');
      }
    }

    setInterval(refreshStatus, 1000);
    refreshStatus();
    scanNetworks();
  </script>
</body>
</html>
)rawliteral";

  page.replace("__SAVED_SSID__", escapeHtml(savedSsidCache));
  page.replace("__SAVED_ROUTE__", escapeHtml(getGyroValidationRoute()));
  page.replace("__GYRO_CHECKED__", isGyroToggleEnabled() ? "checked" : "");
  return page;
}

String buildViewerPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-CAM Viewer</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #07111f;
      --panel: rgba(15, 23, 42, 0.94);
      --panel-border: rgba(148, 163, 184, 0.18);
      --text: #e2e8f0;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --warning: #f59e0b;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color: var(--text);
      background:
        radial-gradient(circle at top left, rgba(56, 189, 248, 0.16), transparent 32%),
        linear-gradient(160deg, #050816, var(--bg));
      padding: 18px;
    }
    .shell {
      max-width: 1100px;
      margin: 0 auto;
      display: grid;
      gap: 16px;
    }
    .grid {
      display: grid;
      grid-template-columns: minmax(0, 1.4fr) minmax(280px, 0.6fr);
      gap: 16px;
    }
    .card {
      background: var(--panel);
      border: 1px solid var(--panel-border);
      border-radius: 20px;
      padding: 18px;
      box-shadow: 0 24px 60px rgba(2, 6, 23, 0.35);
    }
    h1 {
      margin: 0 0 8px;
      font-size: clamp(28px, 5vw, 44px);
      letter-spacing: -0.04em;
    }
    p {
      margin: 0;
      color: var(--muted);
      line-height: 1.6;
    }
    .status {
      padding: 12px 14px;
      border-radius: 12px;
      background: rgba(56, 189, 248, 0.12);
      border: 1px solid rgba(56, 189, 248, 0.2);
      color: #dbeafe;
      margin-bottom: 12px;
      font-size: 14px;
      line-height: 1.5;
    }
    .stream-wrap {
      border-radius: 16px;
      overflow: hidden;
      background: #020617;
      border: 1px solid rgba(148, 163, 184, 0.18);
    }
    img {
      display: block;
      width: 100%;
      height: auto;
      aspect-ratio: 4 / 3;
      object-fit: contain;
      background: #020617;
    }
    .hidden {
      display: none;
    }
    .meta {
      display: grid;
      gap: 10px;
    }
    .meta-row {
      padding: 12px;
      border-radius: 12px;
      background: rgba(2, 6, 23, 0.26);
      border: 1px solid rgba(148, 163, 184, 0.16);
      font-size: 14px;
    }
    .meta-label {
      display: block;
      color: var(--muted);
      font-size: 12px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      margin-bottom: 4px;
    }
    a {
      color: #bae6fd;
    }
    @media (max-width: 900px) {
      .grid {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="shell">
    <div class="card">
      <h1>Camera Feed</h1>
      <p>The live viewer runs here when Wi-Fi is connected. Recording disables the stream until the record button is clicked again.</p>
    </div>

    <div class="grid">
      <div class="card">
        <div id="streamStatus" class="status">Waiting for status...</div>
        <div id="streamWrap" class="stream-wrap">
          <img id="streamImage" src="/stream" alt="Live MJPEG stream" />
        </div>
      </div>

      <div class="card">
        <div class="meta">
          <div class="meta-row"><span class="meta-label">Host</span><span>esp32.local</span></div>
          <div class="meta-row"><span class="meta-label">Station IP</span><span id="stationIpValue">-</span></div>
          <div class="meta-row"><span class="meta-label">Recording</span><span id="recordingValue">Idle</span></div>
          <div class="meta-row"><span class="meta-label">Time Sync</span><span id="timeValue">Not synced</span></div>
          <div class="meta-row"><span class="meta-label">Setup</span><span><a href="/setup">Open configuration page</a></span></div>
        </div>
      </div>
    </div>
  </div>

  <script>
    const statusNode = document.getElementById('streamStatus');
    const streamWrap = document.getElementById('streamWrap');
    const streamImage = document.getElementById('streamImage');

    function updateStreamAvailability(available, recordingActive) {
      if (available && !recordingActive) {
        streamWrap.classList.remove('hidden');
        if (!streamImage.src.includes('/stream')) {
          streamImage.src = '/stream?ts=' + Date.now();
        }
        statusNode.textContent = 'Camera stream is live.';
        return;
      }

      streamWrap.classList.add('hidden');
      streamImage.src = '';
      statusNode.textContent = recordingActive
        ? 'Recording is active. Live streaming resumes when the record button is clicked again.'
        : 'The MJPEG stream is currently unavailable.';
    }

    async function refreshStatus() {
      try {
        const response = await fetch('/status', { cache: 'no-store' });
        const status = await response.json();

        document.getElementById('stationIpValue').textContent = status.stationIp || '-';
        document.getElementById('recordingValue').textContent = status.recordingActive
          ? ('Writing to ' + (status.recordingDirectory || 'session'))
          : 'Idle';
        document.getElementById('timeValue').textContent = status.timeSynced ? 'Synced' : 'Not synced';

        updateStreamAvailability(status.streamAvailable, status.recordingActive);
      } catch (error) {
        statusNode.textContent = 'Unable to fetch current device status.';
      }
    }

    setInterval(refreshStatus, 1000);
    refreshStatus();
  </script>
</body>
</html>
)rawliteral";

  return page;
}

void sendJson(const String& json) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  if (shouldShowViewer()) {
    server.send(200, "text/html", buildViewerPage());
    return;
  }

  server.send(200, "text/html", buildSetupPage());
}

void handleSetup() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", buildSetupPage());
}

void handleStatus() {
  sendJson(buildStatusJson());
}

void handleScan() {
  scanNetworksAsync();
  String json = "{";
  json += "\"inProgress\":";
  json += scanState.inProgress ? "true" : "false";
  json += ",\"error\":\"" + escapeJson(scanState.error) + "\"";
  json += ",\"networks\":";
  json += scanState.jsonCache;
  json += "}";
  sendJson(json);
}

void handleConnect() {
  if (runtimeMode == RuntimeMode::WifiConnecting || runtimeMode == RuntimeMode::PostConnectValidation) {
    sendJson("{\"success\":false,\"error\":\"A connection attempt is already in progress\"}");
    return;
  }

  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String gyroEnabledArg = server.arg("gyroEnabled");
  String validationRoute = server.arg("validationRoute");
  String email = server.arg("email");
  String authPassword = server.arg("authPassword");

  ssid.trim();
  validationRoute.trim();
  email.trim();
  gyroEnabledArg.toLowerCase();

  bool requestedGyroEnabled = (gyroEnabledArg == "1" || gyroEnabledArg == "true" || gyroEnabledArg == "on");

  if (ssid.length() == 0) {
    sendJson("{\"success\":false,\"error\":\"SSID is required\"}");
    return;
  }

  if (requestedGyroEnabled && validationRoute.length() == 0) {
    sendJson("{\"success\":false,\"error\":\"Validation route is required when gyro is enabled\"}");
    return;
  }

  if (requestedGyroEnabled && !gyroHardwareAvailable()) {
    sendJson("{\"success\":false,\"error\":\"Gyro hardware is not available on GPIO3/GPIO13\"}");
    return;
  }

  bool hasEmail = email.length() > 0;
  bool hasPassword = authPassword.length() > 0;
  if (hasEmail != hasPassword) {
    sendJson("{\"success\":false,\"error\":\"Enter both email and password, or leave both empty\"}");
    return;
  }

  PendingConnectionRequest request;
  request.active = true;
  request.keepAccessPoint = apActive || runtimeMode == RuntimeMode::SoftApConfig || runtimeMode == RuntimeMode::ErrorFallback;
  request.ssid = ssid;
  request.password = password;
  request.gyroEnabled = requestedGyroEnabled;
  request.validationRoute = validationRoute;
  request.email = email;
  request.authPassword = authPassword;

  if (!startConnectionAttempt(request)) {
    sendJson("{\"success\":false,\"error\":\"Unable to start the Wi-Fi connection attempt\"}");
    return;
  }

  sendJson("{\"success\":true,\"saved\":false,\"wifiConnected\":false,\"gyroReady\":false,\"recordingReady\":false,\"enteredSoftAp\":false,\"applying\":true,\"lastErrorType\":\"none\",\"lastErrorMessage\":\"\"}");
}

void handleForget() {
  clearCredentials();
  clearConnectionError();
  startConfigMode();
  sendJson("{\"success\":true}");
}

void handleCaptivePortalRedirect() {
  server.sendHeader("Location", "/setup");
  server.send(302, "text/plain", "");
}

void handleNoContent() {
  server.send(204, "text/plain", "");
}

void handleNotFound() {
  if (!shouldShowViewer()) {
    server.sendHeader("Location", "/setup");
    server.send(302, "text/plain", "");
    return;
  }

  server.send(404, "text/plain", "Not found");
}
}  // namespace

void registerRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetup);
  server.on("/setup/", HTTP_GET, handleSetup);
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/generate_204", HTTP_GET, handleCaptivePortalRedirect);
  server.on("/gen_204", HTTP_GET, handleCaptivePortalRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortalRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortalRedirect);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortalRedirect);
  server.on("/success.txt", HTTP_GET, handleCaptivePortalRedirect);
  server.on("/wpad.dat", HTTP_GET, handleNoContent);
  server.on("/favicon.ico", HTTP_GET, handleNoContent);
  server.onNotFound(handleNotFound);
}
