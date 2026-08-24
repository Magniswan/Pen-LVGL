import { Launcher } from 'lvgl_launcher';

function normalize(result) {
  const value = result && typeof result === 'object' ? result : {};
  return {
    available: value.available === true,
    accepted: value.accepted === true,
    holeReady: value.holeReady === true,
    inputReady: value.inputReady === true,
    state: typeof value.state === 'string' ? value.state : 'error',
    sessionPid: Number(value.sessionPid) || 0,
    logicalWidth: Number(value.logicalWidth) || 0,
    logicalHeight: Number(value.logicalHeight) || 0,
    result: Number(value.result) || 0,
    message: typeof value.message === 'string' ? value.message : '',
  };
}

export async function probeLauncher() {
  return normalize(await Launcher.probe());
}

export async function startLauncher() {
  return normalize(await Launcher.start());
}

export async function getLauncherStatus() {
  return normalize(await Launcher.status());
}

export function sendLauncherTouch(payload) {
  return Launcher.sendTouch(payload) === true;
}
