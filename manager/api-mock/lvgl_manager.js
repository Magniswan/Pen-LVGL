let installed = false;
let version = '';

function snapshot(code, detail, success = false) {
  return {
    success,
    available: true,
    payloadAvailable: true,
    installed,
    repairRequired: false,
    updateAvailable: installed && version !== '1.0.0',
    busy: false,
    currentVersion: version,
    payloadVersion: '1.0.0',
    profileId: 'simulator-profile',
    code,
    detail,
  };
}

export const Manager = {
  inspect() {
    return Promise.resolve(snapshot('MANAGER_READY', '仅接受官方发布密钥签名的平台包'));
  },
  install() {
    installed = true;
    version = '1.0.0';
    return Promise.resolve(snapshot('MANAGER_INSTALLED', '平台安装完成', true));
  },
  repair() {
    return Promise.resolve(snapshot('MANAGER_REPAIRED', '平台校验与修复完成', true));
  },
  upgrade() {
    version = '1.0.0';
    return Promise.resolve(snapshot('MANAGER_UPGRADED', '平台升级完成', true));
  },
  remove() {
    installed = false;
    version = '';
    return Promise.resolve(snapshot('MANAGER_REMOVED', '平台已安全移除', true));
  },
};
