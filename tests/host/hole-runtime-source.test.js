const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');
const read = (...parts) => fs.readFileSync(path.join(root, ...parts), 'utf8');

test('hole display uses a certified existing overlay plane without a CRTC modeset', () => {
  const source = read('src', 'platform', 'drm', 'drm_backend.cpp');
  const overlay = source.slice(
    source.indexOf('if(impl_->overlay_mode) {', source.indexOf('present_logical')),
    source.indexOf('if(!impl_->modeset_done)', source.indexOf('present_logical')));
  assert.match(overlay, /drmModeSetPlane/);
  assert.doesNotMatch(overlay, /drmModeSetCrtc/);
  assert.match(source, /DRM_PLANE_TYPE_OVERLAY/);
  assert.match(source, /properties->prop_values\[index\].*overlay_zpos/);
});

test('hole input consumes only the inherited datagram channel', () => {
  const source = read('src', 'platform', 'input', 'input_backend.cpp');
  const forwarded = source.indexOf('forwarded_requested');
  const evdevScan = source.indexOf('for(int index = 0; index < 32;');
  assert.ok(forwarded >= 0 && evdevScan > forwarded);
  assert.match(source, /if\(forwarded_requested\) return impl_->fail/);
  assert.match(source, /MSG_DONTWAIT \| MSG_TRUNC/);
  assert.match(source, /decode_touch_frame/);
});

test('desktop reports ready only after its first successful presentation', () => {
  const source = read('src', 'runtime', 'platform_runtime.cpp');
  const presentation = source.indexOf('drm.has_presented_frame()');
  const ready = source.indexOf('control.signal_ready()');
  assert.ok(presentation >= 0 && ready > presentation);
});
