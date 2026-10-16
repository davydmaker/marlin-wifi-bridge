type ToastType = "success" | "error" | "warning" | "info";

interface ToastOptions {
  message: string;
  type?: ToastType;
  duration?: number;
}

let container: HTMLDivElement | null = null;

function ensureContainer(): HTMLDivElement {
  if (!container) {
    container = document.createElement("div");
    container.id = "toast-container";
    document.body.appendChild(container);
  }
  return container;
}

const ICONS: Record<ToastType, string> = {
  success: `<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>`,
  error: `<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>`,
  warning: `<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>`,
  info: `<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>`,
};

export function showToast(opts: ToastOptions) {
  const { message, type = "info", duration = 3000 } = opts;
  const box = ensureContainer();

  const el = document.createElement("div");
  el.className = `toast toast-${type}`;
  el.innerHTML = `<span class="toast-icon">${ICONS[type]}</span><span class="toast-msg">${message}</span>`;

  box.appendChild(el);

  requestAnimationFrame(() => el.classList.add("toast-show"));

  setTimeout(() => {
    el.classList.remove("toast-show");
    el.classList.add("toast-hide");
    el.addEventListener("transitionend", () => el.remove());
    setTimeout(() => el.remove(), 400);
  }, duration);
}

export function toastSuccess(msg: string) {
  showToast({ message: msg, type: "success" });
}

export function toastError(msg: string) {
  showToast({ message: msg, type: "error", duration: 5000 });
}

export function toastWarning(msg: string) {
  showToast({ message: msg, type: "warning", duration: 4000 });
}

export function toastInfo(msg: string) {
  showToast({ message: msg, type: "info" });
}
