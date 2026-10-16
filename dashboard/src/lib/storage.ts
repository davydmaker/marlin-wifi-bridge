const STORAGE_KEY = "et_temp_history";
const MAX_STORED_POINTS = 600;

interface TempSample {
  ts: number;
  hc: number;
  ht: number;
  bc: number;
  bt: number;
}

export function saveTempHistory(data: TempSample[]) {
  try {
    const trimmed = data.slice(-MAX_STORED_POINTS);
    localStorage.setItem(STORAGE_KEY, JSON.stringify(trimmed));
  } catch {}
}

export function loadTempHistory(): TempSample[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return [];

    const data: TempSample[] = JSON.parse(raw);
    const cutoff = Date.now() - 10 * 60 * 1000;
    return data.filter((s) => s.ts > cutoff);
  } catch {
    return [];
  }
}

export function clearTempHistory() {
  localStorage.removeItem(STORAGE_KEY);
}
