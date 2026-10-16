import { saveTempHistory, loadTempHistory } from "../lib/storage";

let maxPoints = 300;

interface Series {
  label: string;
  color: string;
  data: number[];
}

interface TempSample {
  ts: number;
  hc: number;
  ht: number;
  bc: number;
  bt: number;
}

const series: Series[] = [
  { label: "Hotend", color: "#f97316", data: [] },
  { label: "Hotend Target", color: "#7c3aed", data: [] },
  { label: "Bed", color: "#06b6d4", data: [] },
  { label: "Bed Target", color: "#3b82f6", data: [] },
];

let saveCounter = 0;

let hoverIndex = -1;
let tooltipBound = false;

let lastPad = { top: 20, right: 16, bottom: 24, left: 40 };
let lastPlotW = 0;

export function setMaxPoints(n: number) {
  maxPoints = n;
  for (const s of series) {
    while (s.data.length > maxPoints) s.data.shift();
  }
}

export function restoreFromStorage() {
  const history = loadTempHistory();
  if (history.length === 0) return;

  series[0].data = history.map((s) => s.hc);
  series[1].data = history.map((s) => s.ht);
  series[2].data = history.map((s) => s.bc);
  series[3].data = history.map((s) => s.bt);

  for (const s of series) {
    while (s.data.length > maxPoints) s.data.shift();
  }
}

export function clearChartData() {
  for (const s of series) s.data.length = 0;
}

export function pushTempData(
  hotendCurrent: number,
  hotendTarget: number,
  bedCurrent: number,
  bedTarget: number
) {
  series[0].data.push(hotendCurrent);
  series[1].data.push(hotendTarget);
  series[2].data.push(bedCurrent);
  series[3].data.push(bedTarget);

  for (const s of series) {
    if (s.data.length > maxPoints) {
      s.data.shift();
    }
  }

  saveCounter++;
  if (saveCounter >= 10) {
    saveCounter = 0;
    const samples: TempSample[] = [];
    const len = series[0].data.length;
    const now = Date.now();
    for (let i = 0; i < len; i++) {
      samples.push({
        ts: now - (len - 1 - i) * 1000,
        hc: series[0].data[i],
        ht: series[1].data[i],
        bc: series[2].data[i],
        bt: series[3].data[i],
      });
    }
    saveTempHistory(samples);
  }
}

function bindTooltipEvents(canvas: HTMLCanvasElement) {
  if (tooltipBound) return;
  tooltipBound = true;

  canvas.addEventListener("mousemove", (e) => {
    const rect = canvas.getBoundingClientRect();
    const mouseX = e.clientX - rect.left;
    const numPoints = series[0].data.length;
    if (numPoints < 2 || lastPlotW <= 0) {
      hoverIndex = -1;
      return;
    }

    const relX = mouseX - lastPad.left;
    if (relX < 0 || relX > lastPlotW) {
      hoverIndex = -1;
      return;
    }

    const idx = Math.round((relX / lastPlotW) * (maxPoints - 1));
    hoverIndex = Math.min(Math.max(idx, 0), numPoints - 1);
  });

  canvas.addEventListener("mouseleave", () => {
    hoverIndex = -1;
  });
}

export function drawChart(canvas: HTMLCanvasElement) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();

  if (rect.width === 0 || rect.height === 0) return;

  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);

  bindTooltipEvents(canvas);

  const w = rect.width;
  const h = rect.height;
  const pad = { top: 20, right: 16, bottom: 24, left: 40 };
  const plotW = w - pad.left - pad.right;
  const plotH = h - pad.top - pad.bottom;

  lastPad = pad;
  lastPlotW = plotW;

  ctx.fillStyle = "#1e293b";
  ctx.fillRect(0, 0, w, h);

  let maxVal = 50;
  for (const s of series) {
    for (const v of s.data) {
      if (v > maxVal) maxVal = v;
    }
  }
  maxVal = Math.ceil(maxVal / 50) * 50 + 10;

  ctx.strokeStyle = "#2a3a4e";
  ctx.lineWidth = 0.5;
  ctx.font = "10px monospace";
  ctx.fillStyle = "#64748b";
  ctx.textAlign = "right";

  const gridSteps = 5;
  for (let i = 0; i <= gridSteps; i++) {
    const y = pad.top + plotH - (plotH * i) / gridSteps;
    const val = Math.round((maxVal * i) / gridSteps);

    ctx.beginPath();
    ctx.moveTo(pad.left, y);
    ctx.lineTo(pad.left + plotW, y);
    ctx.stroke();

    ctx.fillText(`${val}`, pad.left - 6, y + 3);
  }

  ctx.fillStyle = "#64748b";
  ctx.textAlign = "left";
  ctx.fillText("°C", pad.left - 6, pad.top - 6);

  const numPoints = series[0].data.length;
  if (numPoints < 2) return;

  for (const s of series) {
    ctx.beginPath();
    ctx.strokeStyle = s.color;
    ctx.lineWidth = s.label.includes("Target") ? 1 : 2;

    if (s.label.includes("Target")) {
      ctx.setLineDash([4, 4]);
    } else {
      ctx.setLineDash([]);
    }

    for (let i = 0; i < s.data.length; i++) {
      const x = pad.left + (plotW * i) / (maxPoints - 1);
      const y = pad.top + plotH - (plotH * s.data[i]) / maxVal;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  ctx.setLineDash([]);

  const legendY = pad.top + 2;
  let legendX = pad.left + 8;
  ctx.font = "10px monospace";
  for (const s of series) {
    ctx.fillStyle = s.color;
    ctx.fillRect(legendX, legendY, 12, 3);
    legendX += 16;
    ctx.fillStyle = "#94a3b8";
    ctx.textAlign = "left";
    ctx.fillText(s.label, legendX, legendY + 4);
    legendX += ctx.measureText(s.label).width + 12;
  }

  if (hoverIndex >= 0 && hoverIndex < numPoints) {
    const hx = pad.left + (plotW * hoverIndex) / (maxPoints - 1);

    ctx.strokeStyle = "rgba(255, 255, 255, 0.3)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(hx, pad.top);
    ctx.lineTo(hx, pad.top + plotH);
    ctx.stroke();

    for (const s of series) {
      if (hoverIndex < s.data.length) {
        const y = pad.top + plotH - (plotH * s.data[hoverIndex]) / maxVal;
        ctx.beginPath();
        ctx.arc(hx, y, 3, 0, Math.PI * 2);
        ctx.fillStyle = s.color;
        ctx.fill();
      }
    }

    const tooltipLines = series
      .filter((s) => hoverIndex < s.data.length)
      .map((s) => `${s.label}: ${s.data[hoverIndex].toFixed(1)}°C`);

    const secsAgo = Math.round((numPoints - 1 - hoverIndex));
    tooltipLines.unshift(`-${secsAgo}s`);

    ctx.font = "10px monospace";
    const lineH = 14;
    const tooltipW = Math.max(...tooltipLines.map((l) => ctx.measureText(l).width)) + 12;
    const tooltipH = tooltipLines.length * lineH + 6;

    let tx = hx + 8;
    if (tx + tooltipW > w - pad.right) {
      tx = hx - tooltipW - 8;
    }
    let ty = pad.top + 10;

    ctx.fillStyle = "rgba(15, 23, 42, 0.9)";
    ctx.strokeStyle = "rgba(100, 116, 139, 0.5)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(tx, ty, tooltipW, tooltipH, 4);
    ctx.fill();
    ctx.stroke();

    ctx.textAlign = "left";
    for (let i = 0; i < tooltipLines.length; i++) {
      if (i === 0) {
        ctx.fillStyle = "#94a3b8";
      } else {
        ctx.fillStyle = series[i - 1].color;
      }
      ctx.fillText(tooltipLines[i], tx + 6, ty + 12 + i * lineH);
    }
  }
}
