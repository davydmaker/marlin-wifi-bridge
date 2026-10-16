import type { WsEvent } from "./types";

type WsEventHandler = (event: WsEvent) => void;
type WsStatusHandler = (connected: boolean) => void;

let ws: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let currentUrl = "";

const eventHandlers: WsEventHandler[] = [];
const statusHandlers: WsStatusHandler[] = [];

const RECONNECT_DELAY_MS = 3000;

function notifyStatus(connected: boolean) {
  statusHandlers.forEach((h) => h(connected));
}

function handleMessage(raw: MessageEvent) {
  try {
    const event: WsEvent = JSON.parse(raw.data);
    eventHandlers.forEach((h) => h(event));
  } catch {
    console.warn("WS: invalid message", raw.data);
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    if (currentUrl) {
      connect(currentUrl);
    }
  }, RECONNECT_DELAY_MS);
}

export function connect(url: string) {
  disconnect();
  currentUrl = url;

  ws = new WebSocket(url);

  ws.onopen = () => {
    console.log("WS: connected");
    notifyStatus(true);
  };

  ws.onclose = () => {
    console.log("WS: disconnected");
    notifyStatus(false);
    scheduleReconnect();
  };

  ws.onerror = (err) => {
    console.error("WS: error", err);
    ws?.close();
  };

  ws.onmessage = handleMessage;
}

export function disconnect() {
  currentUrl = "";
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  if (ws) {
    ws.onclose = null;
    ws.close();
    ws = null;
    notifyStatus(false);
  }
}

export function onEvent(handler: WsEventHandler) {
  eventHandlers.push(handler);
}

export function onStatus(handler: WsStatusHandler) {
  statusHandlers.push(handler);
}
