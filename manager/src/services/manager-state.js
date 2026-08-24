export const OPERATIONS = Object.freeze(['install', 'repair', 'upgrade', 'remove']);

export function normalizeSnapshot(result) {
  const value = result && typeof result === 'object' ? result : {};
  return {
    success: value.success === true,
    available: value.available === true,
    payloadAvailable: value.payloadAvailable === true,
    installed: value.installed === true,
    repairRequired: value.repairRequired === true,
    updateAvailable: value.updateAvailable === true,
    busy: value.busy === true,
    currentVersion: typeof value.currentVersion === 'string' ? value.currentVersion : '',
    payloadVersion: typeof value.payloadVersion === 'string' ? value.payloadVersion : '',
    profileId: typeof value.profileId === 'string' ? value.profileId : '',
    code: typeof value.code === 'string' ? value.code : 'MANAGER_INVALID_RESPONSE',
    detail: typeof value.detail === 'string' ? value.detail : '',
  };
}

export function operationEnabled(snapshot, operation) {
  if (!OPERATIONS.includes(operation) || snapshot.busy === true) return false;
  if (operation === 'remove') return snapshot.installed === true;
  if (snapshot.available !== true || snapshot.payloadAvailable !== true) return false;
  if (operation === 'install') {
    return snapshot.installed !== true && snapshot.repairRequired !== true;
  }
  if (operation === 'repair') {
    return snapshot.installed === true || snapshot.repairRequired === true;
  }
  return snapshot.installed === true && snapshot.updateAvailable === true;
}

export function operationTitle(operation) {
  return {
    install: '安装平台',
    repair: '修复安装',
    upgrade: '升级平台',
    remove: '移除平台',
  }[operation] || '未知操作';
}
