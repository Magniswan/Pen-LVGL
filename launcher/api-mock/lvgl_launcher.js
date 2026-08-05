let state = 'idle';

export const Launcher = {
  probe() {
    return Promise.resolve({
      available: true,
      accepted: false,
      state,
      supervisorPid: 0,
      appPid: 0,
      result: 0,
      message: 'mock ready',
    });
  },
  start() {
    state = 'launching';
    return Promise.resolve({
      available: true,
      accepted: true,
      state,
      supervisorPid: 100,
      appPid: 0,
      result: 0,
      message: 'mock launch accepted',
    });
  },
  status() {
    return Promise.resolve({
      available: true,
      accepted: false,
      state,
      supervisorPid: state === 'idle' ? 0 : 100,
      appPid: 0,
      result: 0,
      message: 'mock status',
    });
  },
};
