let state = 'idle';

export const Launcher = {
  probe() {
    return Promise.resolve({
      available: true,
      accepted: false,
      holeReady: state === 'ready',
      inputReady: state === 'ready',
      state,
      sessionPid: state === 'idle' ? 0 : 100,
      logicalWidth: 960,
      logicalHeight: 266,
      result: 0,
      message: 'mock ready',
    });
  },
  start() {
    state = 'ready';
    return Promise.resolve({
      available: true,
      accepted: true,
      holeReady: true,
      inputReady: true,
      state,
      sessionPid: 100,
      logicalWidth: 960,
      logicalHeight: 266,
      result: 0,
      message: 'mock launch accepted',
    });
  },
  status() {
    return Promise.resolve({
      available: true,
      accepted: false,
      holeReady: state === 'ready',
      inputReady: state === 'ready',
      state,
      sessionPid: state === 'idle' ? 0 : 100,
      logicalWidth: 960,
      logicalHeight: 266,
      result: 0,
      message: 'mock status',
    });
  },
  sendTouch(payload) {
    return state === 'ready' && payload && typeof payload.phase === 'string';
  },
};
