const MAX_LOG_ENTRIES = 200;
const MAX_HISTORY = 50;
const HISTORY_KEY = "et_cmd_history";

let logEl: HTMLElement | null = null;
let autoScroll = true;

let cmdHistory: string[] = [];
let historyIndex = -1;

export function initConsole(elementId: string) {
  logEl = document.getElementById(elementId);

  try {
    const saved = localStorage.getItem(HISTORY_KEY);
    if (saved) cmdHistory = JSON.parse(saved);
  } catch {}
}

export function setAutoScroll(enabled: boolean) {
  autoScroll = enabled;
}

export function clearConsole() {
  if (logEl) logEl.innerHTML = "";
}

function addEntry(text: string, cssClass: string) {
  if (!logEl) return;

  const entry = document.createElement("div");
  entry.className = `log-entry ${cssClass}`;

  const time = new Date().toLocaleTimeString("pt-BR", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
  entry.textContent = `[${time}] ${text}`;

  logEl.appendChild(entry);

  while (logEl.children.length > MAX_LOG_ENTRIES) {
    logEl.removeChild(logEl.firstChild!);
  }

  if (autoScroll) {
    logEl.scrollTop = logEl.scrollHeight;
  }
}

export function logTx(cmd: string) {
  addEntry(`>> ${cmd}`, "log-tx");
}

export function logRx(msg: string) {
  addEntry(`<< ${msg}`, "log-rx");
}

export function logError(msg: string) {
  addEntry(`!! ${msg}`, "log-err");
}

export function pushHistory(cmd: string) {
  if (cmdHistory.length > 0 && cmdHistory[cmdHistory.length - 1] === cmd) {
    historyIndex = -1;
    return;
  }

  cmdHistory.push(cmd);
  if (cmdHistory.length > MAX_HISTORY) {
    cmdHistory.shift();
  }

  historyIndex = -1;

  try {
    localStorage.setItem(HISTORY_KEY, JSON.stringify(cmdHistory));
  } catch {}
}

export function historyUp(): string | null {
  if (cmdHistory.length === 0) return null;

  if (historyIndex === -1) {
    historyIndex = cmdHistory.length - 1;
  } else if (historyIndex > 0) {
    historyIndex--;
  }

  return cmdHistory[historyIndex];
}

export function historyDown(): string | null {
  if (historyIndex === -1) return null;

  if (historyIndex < cmdHistory.length - 1) {
    historyIndex++;
    return cmdHistory[historyIndex];
  }

  historyIndex = -1;
  return "";
}
