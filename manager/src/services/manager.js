import { Manager } from 'lvgl_manager';
import { normalizeSnapshot } from './manager-state.js';

export async function inspectManager() {
  return normalizeSnapshot(await Manager.inspect());
}

export async function runManagerOperation(operation) {
  if (!['install', 'repair', 'upgrade', 'remove'].includes(operation)) {
    throw new Error('不支持的管理操作');
  }
  return normalizeSnapshot(await Manager[operation]());
}
