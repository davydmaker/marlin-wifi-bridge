import type { TelemetryPayload, CommandResponse, PollingConfig } from "./types";

let baseUrl = "";

export function setBaseUrl(ip: string) {
  baseUrl = `http://${ip}`;
}

export function getWsUrl(ip: string): string {
  return `ws://${ip}/ws`;
}

async function request<T>(
  path: string,
  options?: RequestInit
): Promise<T> {
  const res = await fetch(`${baseUrl}${path}`, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...options?.headers,
    },
  });

  if (!res.ok) {
    const body = await res.text();
    throw new Error(`HTTP ${res.status}: ${body}`);
  }

  return res.json();
}

export async function fetchTelemetry(): Promise<TelemetryPayload> {
  return request<TelemetryPayload>("/api/telemetry");
}

export async function sendCommand(
  gcode: string,
  confirm = false
): Promise<CommandResponse> {
  const headers: Record<string, string> = {};
  if (confirm) {
    headers["X-Confirm"] = "true";
  }
  return request<CommandResponse>("/api/command", {
    method: "POST",
    headers,
    body: JSON.stringify({ gcode }),
  });
}

export async function setTemperature(
  hotend?: number,
  bed?: number
): Promise<CommandResponse> {
  const body: Record<string, number> = {};
  if (hotend !== undefined) body.hotend = hotend;
  if (bed !== undefined) body.bed = bed;
  return request<CommandResponse>("/api/temperature", {
    method: "POST",
    body: JSON.stringify(body),
  });
}

export async function postAction(action: string): Promise<CommandResponse> {
  return request<CommandResponse>(`/api/${action}`, {
    method: "POST",
    headers: { "X-Confirm": "true" },
  });
}

export async function getPollingConfig(): Promise<PollingConfig> {
  return request<PollingConfig>("/api/config/polling");
}

export async function setPollingConfig(
  interval_ms: number
): Promise<CommandResponse & { warning?: string }> {
  return request<CommandResponse & { warning?: string }>("/api/config/polling", {
    method: "POST",
    body: JSON.stringify({ interval_ms }),
  });
}

export interface FileListResponse {
  ok: boolean;
  files: string[];
  error?: string;
}

export async function getFileList(): Promise<FileListResponse> {
  return request<FileListResponse>("/api/files");
}

export async function startPrint(filename: string): Promise<CommandResponse> {
  return request<CommandResponse>("/api/print/start", {
    method: "POST",
    body: JSON.stringify({ filename }),
  });
}

export async function deleteFile(filename: string): Promise<CommandResponse> {
  return request<CommandResponse>("/api/files/delete", {
    method: "POST",
    body: JSON.stringify({ filename }),
  });
}
