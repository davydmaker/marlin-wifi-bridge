export interface ThermalData {
  hotend: { current: number; target: number };
  bed: { current: number; target: number };
}

export interface PositionData {
  x: number;
  y: number;
  z: number;
  e: number;
}

export interface PrintData {
  state: "idle" | "printing" | "paused" | "finished" | "error";
  progress_pct: number;
  byte_pos: number;
  byte_total: number;
  filename: string;
  speed_override: number;
  flow_override: number;
  fan_speed: number;
}

export interface CommData {
  state: "connected" | "disconnected" | "error";
  errors: number;
  timeouts: number;
  tx_cmds: number;
  rx_bytes: number;
  latency_ms: number;
}

export interface DeviceData {
  heap_free: number;
  uptime_s: number;
  wifi_rssi: number;
  chip_temp?: number;
}

export interface TelemetryPayload {
  ts: number;
  device: DeviceData;
  comm: CommData;
  thermal: ThermalData;
  position: PositionData;
  print: PrintData;
}

export interface WsEvent {
  event: "thermal" | "position" | "print" | "comm";
  ts: number;
  data: ThermalData | PositionData | PrintData | CommData;
}

export interface CommandResponse {
  ok: boolean;
  error?: string;
  warning?: string;
}

export interface PollingConfig {
  interval_ms: number;
  warning?: string;
}
