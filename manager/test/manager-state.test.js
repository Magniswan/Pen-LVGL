const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

async function loadStateModule() {
  const source = fs.readFileSync(
    path.resolve(__dirname, '../src/services/manager-state.js'), 'utf8');
  return import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
}

test('normalizes native values without trusting truthy coercions', async () => {
  const { normalizeSnapshot } = await loadStateModule();
  const result = normalizeSnapshot({
    success: 'yes',
    available: 1,
    payloadAvailable: true,
    installed: 'yes',
    currentVersion: 2,
    code: null,
  });
  assert.equal(result.available, false);
  assert.equal(result.success, false);
  assert.equal(result.payloadAvailable, true);
  assert.equal(result.installed, false);
  assert.equal(result.currentVersion, '');
  assert.equal(result.code, 'MANAGER_INVALID_RESPONSE');
});

test('enables only operations valid for the observed state', async () => {
  const { operationEnabled } = await loadStateModule();
  const absent = { available: true, payloadAvailable: true, installed: false, busy: false };
  assert.equal(operationEnabled(absent, 'install'), true);
  assert.equal(operationEnabled(absent, 'repair'), false);
  assert.equal(operationEnabled(absent, 'upgrade'), false);
  assert.equal(operationEnabled(absent, 'remove'), false);
  const current = { ...absent, installed: true, updateAvailable: false };
  assert.equal(operationEnabled(current, 'install'), false);
  assert.equal(operationEnabled(current, 'repair'), true);
  assert.equal(operationEnabled(current, 'upgrade'), false);
  assert.equal(operationEnabled(current, 'remove'), true);
  assert.equal(operationEnabled({ ...current, updateAvailable: true }, 'upgrade'), true);
  assert.equal(operationEnabled({ ...current, busy: true }, 'remove'), false);
  const missingPayload = { ...absent, repairRequired: true };
  assert.equal(operationEnabled(missingPayload, 'install'), false);
  assert.equal(operationEnabled(missingPayload, 'repair'), true);
});
