import { Launcher } from 'lvgl_launcher';

function normalize(result) {
  const value = result && typeof result === 'object' ? result : {};
  return {
    available: value.available === true,
    accepted: value.accepted === true,
    state: typeof value.state === 'string' ? value.state : 'error',
    supervisorPid: Number(value.supervisorPid) || 0,
    appPid: Number(value.appPid) || 0,
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
