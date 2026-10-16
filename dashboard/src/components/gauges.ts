const TAU = Math.PI * 2;
const ARC_START = 0.75 * TAU;
const ARC_SPAN = 0.75 * TAU;

interface GaugeConfig {
  canvas: HTMLCanvasElement;
  maxValue: number;
  color: string;
  bgColor: string;
}

export function drawGauge(
  config: GaugeConfig,
  current: number,
  target: number
) {
  const { canvas, maxValue, color, bgColor } = config;
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const w = canvas.width;
  const h = canvas.height;
  const cx = w / 2;
  const cy = h / 2;
  const r = Math.min(cx, cy) - 12;
  const lineWidth = 10;

  ctx.clearRect(0, 0, w, h);

  ctx.beginPath();
  ctx.arc(cx, cy, r, ARC_START, ARC_START + ARC_SPAN);
  ctx.strokeStyle = bgColor;
  ctx.lineWidth = lineWidth;
  ctx.lineCap = "round";
  ctx.stroke();

  const pct = Math.min(current / maxValue, 1);
  if (pct > 0) {
    ctx.beginPath();
    ctx.arc(cx, cy, r, ARC_START, ARC_START + ARC_SPAN * pct);
    ctx.strokeStyle = color;
    ctx.lineWidth = lineWidth;
    ctx.lineCap = "round";
    ctx.stroke();
  }

  if (target > 0) {
    const targetPct = Math.min(target / maxValue, 1);
    const targetAngle = ARC_START + ARC_SPAN * targetPct;
    const tickInner = r - lineWidth;
    const tickOuter = r + lineWidth;

    ctx.beginPath();
    ctx.moveTo(
      cx + tickInner * Math.cos(targetAngle),
      cy + tickInner * Math.sin(targetAngle)
    );
    ctx.lineTo(
      cx + tickOuter * Math.cos(targetAngle),
      cy + tickOuter * Math.sin(targetAngle)
    );
    ctx.strokeStyle = "#f1f5f9";
    ctx.lineWidth = 2;
    ctx.lineCap = "round";
    ctx.stroke();
  }
}

export function createGaugeConfig(
  canvasId: string,
  maxValue: number,
  color: string
): GaugeConfig {
  const canvas = document.getElementById(canvasId) as HTMLCanvasElement;
  return {
    canvas,
    maxValue,
    color,
    bgColor: "#1e293b",
  };
}
