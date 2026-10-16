import {
  setBaseUrl,
  fetchTelemetry,
  sendCommand,
  setTemperature,
  postAction,
  getPollingConfig,
  setPollingConfig,
  getFileList,
  startPrint,
  deleteFile,
  getWsUrl,
} from "./lib/api";
import { connect, disconnect, onEvent, onStatus } from "./lib/websocket";
import { createGaugeConfig, drawGauge } from "./components/gauges";
import {
  pushTempData,
  drawChart,
  setMaxPoints,
  restoreFromStorage,
  clearChartData,
} from "./components/chart";
import {
  initConsole,
  logTx,
  logRx,
  logError,
  setAutoScroll,
  clearConsole,
  pushHistory,
  historyUp,
  historyDown,
} from "./components/console";
import { toastSuccess, toastError, toastWarning, toastInfo } from "./components/toast";
import { clearTempHistory } from "./lib/storage";
import type {
  TelemetryPayload,
  ThermalData,
  PositionData,
  PrintData,
  CommData,
} from "./lib/types";

const $ = <T extends HTMLElement>(id: string) =>
  document.getElementById(id) as T;

let connected = false;
let printerConnected = false;
let pollTimer: ReturnType<typeof setInterval> | null = null;
let chartTimer: ReturnType<typeof setInterval> | null = null;
let jogStep = 10;
let currentPrintState: PrintData["state"] = "idle";
let jogConfirmedAt = 0;
const JOG_CONFIRM_COOLDOWN_MS = 2 * 60 * 1000;

function switchTab(tabName: string) {
  document.querySelectorAll(".tab-content").forEach((el) => {
    el.classList.toggle("active", el.id === `tab-${tabName}`);
  });
  document.querySelectorAll(".tab-btn").forEach((btn) => {
    btn.classList.toggle("active", (btn as HTMLElement).dataset.tab === tabName);
  });
  location.hash = tabName;

  if (tabName === "monitor") {
    requestAnimationFrame(() => drawChart($<HTMLCanvasElement>("temp-chart")));
  }
}

let hotendGauge: ReturnType<typeof createGaugeConfig>;
let bedGauge: ReturnType<typeof createGaugeConfig>;

function updateThermal(data: ThermalData) {
  $("hotend-temp").textContent = data.hotend.current.toFixed(0);
  $("hotend-target").textContent = data.hotend.target.toFixed(0);
  $("bed-temp").textContent = data.bed.current.toFixed(0);
  $("bed-target").textContent = data.bed.target.toFixed(0);

  drawGauge(hotendGauge, data.hotend.current, data.hotend.target);
  drawGauge(bedGauge, data.bed.current, data.bed.target);

  pushTempData(
    data.hotend.current,
    data.hotend.target,
    data.bed.current,
    data.bed.target
  );
}

function updatePosition(data: PositionData) {
  $("pos-x").textContent = data.x.toFixed(1);
  $("pos-y").textContent = data.y.toFixed(1);
  $("pos-z").textContent = data.z.toFixed(2);
  $("pos-e").textContent = data.e.toFixed(0);
}

function updatePrint(data: PrintData) {
  const stateEl = $("print-state");
  stateEl.textContent = data.state.toUpperCase();
  stateEl.className = `print-state-badge ${data.state}`;

  $("print-pct").textContent = `${data.progress_pct.toFixed(1)}%`;
  $("print-progress").style.width = `${data.progress_pct}%`;

  const fmt = (n: number) => {
    if (n > 1048576) return `${(n / 1048576).toFixed(1)} MB`;
    if (n > 1024) return `${(n / 1024).toFixed(0)} KB`;
    return `${n} B`;
  };
  $("print-bytes").textContent = `${fmt(data.byte_pos)} / ${fmt(data.byte_total)}`;

  const fnEl = $("print-filename");
  if (data.filename) {
    fnEl.textContent = data.filename;
    fnEl.style.display = "block";
  } else {
    fnEl.style.display = "none";
  }

  if (data.speed_override !== undefined) {
    $("mon-speed").textContent = `${data.speed_override}%`;
  }
  if (data.flow_override !== undefined) {
    $("mon-flow").textContent = `${data.flow_override}%`;
  }
  if (data.fan_speed !== undefined) {
    const pct = Math.round((data.fan_speed / 255) * 100);
    $("mon-fan").textContent = `${pct}%`;
  }

  currentPrintState = data.state;
  updatePrintActions(data.state);
}

function updatePrintActions(state: PrintData["state"]) {
  const btnPause = $<HTMLButtonElement>("btn-pause");
  const btnResume = $<HTMLButtonElement>("btn-resume");
  const btnCancel = $<HTMLButtonElement>("btn-cancel");
  const btnRefresh = $<HTMLButtonElement>("btn-refresh-files");

  const isPrinting = state === "printing";
  const isPaused = state === "paused";
  const isActive = isPrinting || isPaused;

  btnPause.disabled = !isPrinting || !printerConnected;

  btnResume.disabled = !isPaused || !printerConnected;

  btnCancel.disabled = !isActive || !printerConnected;

  btnRefresh.disabled = isActive || !printerConnected;
  document.querySelectorAll<HTMLButtonElement>("#file-list-container .file-actions button").forEach((btn) => {
    btn.disabled = isActive;
  });
}

function setPrinterControlsEnabled(enabled: boolean) {
  printerConnected = enabled;

  const buttonIds = [
    "btn-set-hotend", "btn-off-hotend", "btn-set-bed", "btn-off-bed",
    "btn-home-jog",
    "btn-estop",
    "btn-send-gcode",
  ];
  const inputIds = [
    "ctrl-hotend-temp", "ctrl-bed-temp",
    "gcode-input",
  ];

  for (const id of buttonIds) {
    const el = document.getElementById(id) as HTMLButtonElement | null;
    if (el) el.disabled = !enabled;
  }
  for (const id of inputIds) {
    const el = document.getElementById(id) as HTMLInputElement | null;
    if (el) el.disabled = !enabled;
  }

  document.querySelectorAll<HTMLButtonElement>(".jog-btn, .btn-step").forEach((btn) => {
    btn.disabled = !enabled;
  });

  document.body.classList.toggle("printer-offline", !enabled);

  updatePrintActions(currentPrintState);
}

function updateComm(data: CommData) {
  $("comm-state").textContent = data.state;
  $("comm-state").className =
    "stat-value" + (data.state === "error" ? " error" : "");
  $("comm-latency").textContent = `${data.latency_ms} ms`;
  $("comm-tx").textContent = data.tx_cmds.toLocaleString();
  $("comm-rx").textContent = formatBytes(data.rx_bytes);
  $("comm-errors").textContent = data.errors.toString();
  $("comm-errors").className =
    "stat-value" + (data.errors > 0 ? " error" : "");
  $("comm-timeouts").textContent = data.timeouts.toString();
  $("comm-timeouts").className =
    "stat-value" + (data.timeouts > 0 ? " error" : "");

  const isConnected = data.state === "connected";
  if (isConnected !== printerConnected) {
    setPrinterControlsEnabled(isConnected);
  }
}

function updateDevice(data: TelemetryPayload["device"]) {
  $("dev-heap").textContent = formatBytes(data.heap_free);
  $("dev-uptime").textContent = formatUptime(data.uptime_s);
  $("dev-rssi").textContent = `${data.wifi_rssi} dBm`;
  if (data.chip_temp !== undefined) {
    $("dev-chip-temp").textContent = `${data.chip_temp.toFixed(1)} °C`;
  }
}

function updateAll(data: TelemetryPayload) {
  updateThermal(data.thermal);
  updatePosition(data.position);
  updatePrint(data.print);
  updateComm(data.comm);
  updateDevice(data.device);
}

function formatBytes(n: number): string {
  if (n > 1048576) return `${(n / 1048576).toFixed(1)} MB`;
  if (n > 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${n} B`;
}

function formatUptime(secs: number): string {
  const h = Math.floor(secs / 3600);
  const m = Math.floor((secs % 3600) / 60);
  const s = secs % 60;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function setBadge(state: "connected" | "connecting" | "disconnected") {
  const badge = $("connection-badge");
  badge.textContent = state.toUpperCase();
  badge.className = `badge badge-${state}`;
}

async function loadFileList() {
  const container = $("file-list-container");
  container.innerHTML = '<div class="file-loading">Loading files...</div>';

  try {
    const res = await getFileList();
    if (!res.ok) {
      container.innerHTML = `<div class="file-empty">Error: ${res.error || "Unknown"}</div>`;
      toastError("Failed to load file list");
      return;
    }

    if (res.files.length === 0) {
      container.innerHTML = '<div class="file-empty">No files on SD card</div>';
      toastInfo("SD card is empty");
      return;
    }

    container.innerHTML = "";
    const list = document.createElement("div");
    list.className = "file-list";

    for (const file of res.files) {
      const item = document.createElement("div");
      item.className = "file-item";

      const name = document.createElement("span");
      name.className = "file-name";
      name.textContent = file;

      const actions = document.createElement("div");
      actions.className = "file-actions";

      const printBtn = document.createElement("button");
      printBtn.textContent = "Print";
      printBtn.onclick = async () => {
        if (!confirm(`Start printing "${file}"?`)) return;
        logTx(`M23 ${file} + M24`);
        try {
          const r = await startPrint(file);
          if (r.ok) {
            toastSuccess(`Started printing: ${file}`);
            logRx("Print started");
          } else {
            toastError(`Failed: ${r.error}`);
            logError(`Print failed: ${r.error}`);
          }
        } catch (err) {
          toastError(`Failed to start print`);
          logError(`Failed: ${err}`);
        }
      };

      const delBtn = document.createElement("button");
      delBtn.textContent = "Delete";
      delBtn.className = "btn-danger";
      delBtn.onclick = async () => {
        if (!confirm(`Delete "${file}" from SD card?`)) return;
        logTx(`M30 ${file}`);
        try {
          const r = await deleteFile(file);
          if (r.ok) {
            toastSuccess(`Deleted: ${file}`);
            logRx("File deleted");
            loadFileList();
          } else {
            toastError(`Delete failed: ${r.error}`);
            logError(`Delete failed: ${r.error}`);
          }
        } catch (err) {
          toastError(`Failed to delete file`);
          logError(`Failed: ${err}`);
        }
      };

      actions.appendChild(printBtn);
      actions.appendChild(delBtn);
      item.appendChild(name);
      item.appendChild(actions);
      list.appendChild(item);
    }

    container.appendChild(list);
    toastSuccess(`Found ${res.files.length} file(s) on SD`);
  } catch (err) {
    container.innerHTML = '<div class="file-empty">Failed to load files</div>';
    toastError("Failed to connect for file list");
    logError(`File list error: ${err}`);
  }
}

const ipInput = () => $<HTMLInputElement>("esp-ip");
const btnConnect = () => $<HTMLButtonElement>("btn-connect");

async function doConnect() {
  const ip = ipInput().value.trim();
  if (!ip) return;

  localStorage.setItem("esp32_ip", ip);
  setBadge("connecting");
  setBaseUrl(ip);

  try {
    const data = await fetchTelemetry();
    updateAll(data);
    connected = true;
    setBadge("connected");
    toastSuccess("Connected to ESP32");

    $("cfg-ip").textContent = ip;

    connect(getWsUrl(ip));

    if (pollTimer) clearInterval(pollTimer);
    pollTimer = setInterval(async () => {
      try {
        const d = await fetchTelemetry();
        updateAll(d);
      } catch {}
    }, 5000);

    if (chartTimer) clearInterval(chartTimer);
    chartTimer = setInterval(() => drawChart($<HTMLCanvasElement>("temp-chart")), 1000);

    try {
      const cfg = await getPollingConfig();
      $<HTMLInputElement>("cfg-polling").value = cfg.interval_ms.toString();
      $("polling-value").textContent = `${cfg.interval_ms}ms`;
    } catch {}

    logRx("Connected to ESP32");
  } catch (err) {
    setBadge("disconnected");
    toastError("Connection failed");
    logError(`Connection failed: ${err}`);
  }
}

function resetMetrics() {
  $("hotend-temp").textContent = "--";
  $("hotend-target").textContent = "--";
  $("bed-temp").textContent = "--";
  $("bed-target").textContent = "--";
  drawGauge(hotendGauge, 0, 0);
  drawGauge(bedGauge, 0, 0);

  $("pos-x").textContent = "--";
  $("pos-y").textContent = "--";
  $("pos-z").textContent = "--";
  $("pos-e").textContent = "--";

  $("print-state").textContent = "--";
  $("print-state").className = "print-state-badge";
  $("print-pct").textContent = "--%";
  $("print-progress").style.width = "0%";
  $("print-bytes").textContent = "-- / --";
  $("print-filename").textContent = "";
  $("print-filename").style.display = "none";
  $("mon-speed").textContent = "--%";
  $("mon-flow").textContent = "--%";
  $("mon-fan").textContent = "--%";
  currentPrintState = "idle";

  $("comm-state").textContent = "disconnected";
  $("comm-state").className = "stat-value";
  $("comm-latency").textContent = "-- ms";
  $("comm-tx").textContent = "--";
  $("comm-rx").textContent = "--";
  $("comm-errors").textContent = "--";
  $("comm-errors").className = "stat-value";
  $("comm-timeouts").textContent = "--";
  $("comm-timeouts").className = "stat-value";

  $("dev-heap").textContent = "--";
  $("dev-uptime").textContent = "--";
  $("dev-rssi").textContent = "-- dBm";
  $("dev-chip-temp").textContent = "-- °C";
}

function doDisconnect() {
  disconnect();
  connected = false;
  setBadge("disconnected");
  $("cfg-ws-status").textContent = "Disconnected";
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
  if (chartTimer) {
    clearInterval(chartTimer);
    chartTimer = null;
  }
  resetMetrics();
  setPrinterControlsEnabled(false);
  toastInfo("Disconnected");
}

onEvent((event) => {
  switch (event.event) {
    case "thermal":
      updateThermal(event.data as ThermalData);
      break;
    case "position":
      updatePosition(event.data as PositionData);
      break;
    case "print":
      updatePrint(event.data as PrintData);
      break;
    case "comm":
      updateComm(event.data as CommData);
      break;
  }
});

onStatus((wsConnected) => {
  $("cfg-ws-status").textContent = wsConnected ? "Connected" : "Reconnecting...";
  if (wsConnected) {
    setBadge("connected");
  } else if (connected) {
    setBadge("connecting");
  }
});

function setupControls() {
  $<HTMLButtonElement>("btn-set-hotend").onclick = async () => {
    const val = parseInt($<HTMLInputElement>("ctrl-hotend-temp").value);
    if (!isNaN(val)) {
      logTx(`M104 S${val}`);
      try {
        const res = await setTemperature(val, undefined);
        logRx(res.ok ? "ok" : `Error: ${res.error}`);
        if (res.ok) toastSuccess(`Hotend target: ${val}°C`);
        else toastError(`Failed: ${res.error}`);
      } catch (err) {
        toastError("Failed to set hotend temp");
        logError(`Failed: ${err}`);
      }
    }
  };

  $<HTMLButtonElement>("btn-off-hotend").onclick = async () => {
    logTx("M104 S0");
    try {
      const res = await setTemperature(0, undefined);
      logRx(res.ok ? "ok" : `Error: ${res.error}`);
      if (res.ok) toastSuccess("Hotend off");
      else toastError(`Failed: ${res.error}`);
    } catch (err) {
      toastError("Failed to turn off hotend");
      logError(`Failed: ${err}`);
    }
  };

  $<HTMLButtonElement>("btn-set-bed").onclick = async () => {
    const val = parseInt($<HTMLInputElement>("ctrl-bed-temp").value);
    if (!isNaN(val)) {
      logTx(`M140 S${val}`);
      try {
        const res = await setTemperature(undefined, val);
        logRx(res.ok ? "ok" : `Error: ${res.error}`);
        if (res.ok) toastSuccess(`Bed target: ${val}°C`);
        else toastError(`Failed: ${res.error}`);
      } catch (err) {
        toastError("Failed to set bed temp");
        logError(`Failed: ${err}`);
      }
    }
  };

  $<HTMLButtonElement>("btn-off-bed").onclick = async () => {
    logTx("M140 S0");
    try {
      const res = await setTemperature(undefined, 0);
      logRx(res.ok ? "ok" : `Error: ${res.error}`);
      if (res.ok) toastSuccess("Bed off");
      else toastError(`Failed: ${res.error}`);
    } catch (err) {
      toastError("Failed to turn off bed");
      logError(`Failed: ${err}`);
    }
  };

  document.querySelectorAll<HTMLButtonElement>(".jog-btn[data-axis]").forEach((btn) => {
    btn.onclick = async () => {
      if (currentPrintState === "printing" || currentPrintState === "paused") {
        const now = Date.now();
        if (now - jogConfirmedAt > JOG_CONFIRM_COOLDOWN_MS) {
          if (!confirm("The printer is busy with a print. Move the axes anyway?")) return;
          jogConfirmedAt = now;
        }
      }

      const axis = btn.dataset.axis!;
      const dir = parseInt(btn.dataset.dir!);
      const dist = jogStep * dir;
      const speed = axis === "Z" ? 600 : 3000;
      const cmds = `G91\nG0 ${axis}${dist} F${speed}\nG90`;
      logTx(`Jog ${axis} ${dist > 0 ? "+" : ""}${dist}mm`);
      try {
        const res = await sendCommand(cmds, true);
        logRx(res.ok ? "ok" : `Error: ${res.error}`);
        if (!res.ok) toastError(`Jog failed: ${res.error}`);
      } catch (err) {
        toastError("Jog command failed");
        logError(`Failed: ${err}`);
      }
    };
  });

  $<HTMLButtonElement>("btn-home-jog").onclick = async () => {
    logTx("G28");
    try {
      const res = await sendCommand("G28", true);
      logRx(res.ok ? "ok" : `Error: ${res.error}`);
      if (res.ok) toastSuccess("Homing...");
      else toastError(`Home failed: ${res.error}`);
    } catch (err) {
      toastError("Home command failed");
      logError(`Failed: ${err}`);
    }
  };

  document.querySelectorAll<HTMLButtonElement>(".btn-step").forEach((btn) => {
    btn.onclick = () => {
      jogStep = parseFloat(btn.dataset.step!);
      document.querySelectorAll(".btn-step").forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
    };
  });

  const actions: [string, string, string][] = [
    ["btn-pause", "print/pause", "Print paused"],
    ["btn-resume", "print/resume", "Print resumed"],
    ["btn-cancel", "print/cancel", "Print cancelled"],
    ["btn-estop", "emergency_stop", "Emergency stop sent!"],
  ];

  for (const [btnId, action, successMsg] of actions) {
    $<HTMLButtonElement>(btnId).onclick = async () => {
      if (action === "emergency_stop" || action === "print/cancel") {
        if (!confirm(`Execute ${action.toUpperCase()}?`)) return;
      }

      logTx(action);
      try {
        const res = await postAction(action);
        logRx(res.ok ? "ok" : `Error: ${res.error}`);
        if (res.ok) {
          if (action === "emergency_stop") toastWarning(successMsg);
          else toastSuccess(successMsg);
        } else {
          toastError(`Failed: ${res.error}`);
        }
      } catch (err) {
        toastError(`${action} failed`);
        logError(`Failed: ${err}`);
      }
    };
  }

  $<HTMLButtonElement>("btn-refresh-files").onclick = () => {
    if (!connected) {
      toastWarning("Not connected");
      return;
    }
    loadFileList();
  };

  const gcodeInput = $<HTMLInputElement>("gcode-input");

  const sendGcode = async () => {
    const cmd = gcodeInput.value.trim().toUpperCase();
    if (!cmd) return;

    pushHistory(cmd);
    logTx(cmd);
    gcodeInput.value = "";

    try {
      const res = await sendCommand(cmd, true);
      logRx(res.ok ? "ok" : `Error: ${res.error}`);
      if (!res.ok) toastError(`Command failed: ${res.error}`);
    } catch (err) {
      toastError("Command failed");
      logError(`Failed: ${err}`);
    }
  };

  $<HTMLButtonElement>("btn-send-gcode").onclick = sendGcode;
  gcodeInput.onkeydown = (e) => {
    if (e.key === "Enter") {
      sendGcode();
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      const cmd = historyUp();
      if (cmd !== null) gcodeInput.value = cmd;
    } else if (e.key === "ArrowDown") {
      e.preventDefault();
      const cmd = historyDown();
      if (cmd !== null) gcodeInput.value = cmd;
    }
  };

  $<HTMLInputElement>("console-autoscroll").onchange = (e) => {
    setAutoScroll((e.target as HTMLInputElement).checked);
  };
  $<HTMLButtonElement>("btn-console-clear").onclick = clearConsole;
}

function setupSettings() {
  const pollingSlider = $<HTMLInputElement>("cfg-polling");
  const pollingValue = $("polling-value");
  const pollingWarning = $("polling-warning");

  pollingSlider.oninput = () => {
    const val = parseInt(pollingSlider.value);
    pollingValue.textContent = `${val}ms`;
    pollingWarning.style.display = val < 500 ? "block" : "none";
  };

  $<HTMLButtonElement>("btn-save-polling").onclick = async () => {
    const val = parseInt(pollingSlider.value);
    logTx(`Set polling: ${val}ms`);
    try {
      const res = await setPollingConfig(val);
      logRx(res.ok ? "Polling updated" : `Error: ${res.error}`);
      if (res.ok) {
        toastSuccess(`Polling: ${val}ms`);
        if (res.warning) toastWarning(res.warning);
      } else {
        toastError(`Failed: ${res.error}`);
      }
    } catch (err) {
      toastError("Failed to update polling");
      logError(`Failed: ${err}`);
    }
  };

  document.querySelectorAll<HTMLInputElement>('input[name="chart-duration"]').forEach((radio) => {
    radio.onchange = () => {
      const val = parseInt(radio.value);
      setMaxPoints(val);
      localStorage.setItem("et_chart_duration", val.toString());
      toastSuccess(`Chart: ${val / 60} min`);
    };
  });

  const savedDuration = localStorage.getItem("et_chart_duration");
  if (savedDuration) {
    const val = parseInt(savedDuration);
    setMaxPoints(val);
    const radio = document.querySelector<HTMLInputElement>(
      `input[name="chart-duration"][value="${val}"]`
    );
    if (radio) radio.checked = true;
  }

  $<HTMLButtonElement>("btn-clear-history").onclick = () => {
    clearTempHistory();
    clearChartData();
    toastSuccess("History cleared");
    logRx("Temperature history cleared");
  };
}

function init() {
  const savedIp = localStorage.getItem("esp32_ip");
  if (savedIp) {
    ipInput().value = savedIp;
  }

  document.querySelectorAll<HTMLButtonElement>(".tab-btn").forEach((btn) => {
    btn.onclick = () => switchTab(btn.dataset.tab!);
  });

  const hash = location.hash.replace("#", "");
  if (hash && document.getElementById(`tab-${hash}`)) {
    switchTab(hash);
  }

  hotendGauge = createGaugeConfig("gauge-hotend", 280, "#f97316");
  bedGauge = createGaugeConfig("gauge-bed", 120, "#06b6d4");
  drawGauge(hotendGauge, 0, 0);
  drawGauge(bedGauge, 0, 0);

  initConsole("console-log");

  restoreFromStorage();

  setupControls();
  setupSettings();

  btnConnect().onclick = () => {
    if (connected) {
      doDisconnect();
      btnConnect().textContent = "Connect";
    } else {
      doConnect();
      btnConnect().textContent = "Disconnect";
    }
  };

  ipInput().onkeydown = (e) => {
    if (e.key === "Enter" && !connected) {
      doConnect();
      btnConnect().textContent = "Disconnect";
    }
  };

  drawChart($<HTMLCanvasElement>("temp-chart"));

  setPrinterControlsEnabled(false);
}

document.addEventListener("DOMContentLoaded", init);
