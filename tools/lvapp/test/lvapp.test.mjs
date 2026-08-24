import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
import { mkdtemp, mkdir, readFile, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  buildDevelopmentPackage, parsePackage, signPackage, validatePackagePath,
  verifyPackageFile,
} from '../lib.mjs';

function manifest() {
  return {
    formatVersion: 1,
    appId: 'top.lvgl.game2048',
    name: '2048',
    version: '1.0.0',
    releaseCounter: 1,
    securityEpoch: 0,
    sdkAbi: '1.0',
    minPlatformVersion: '1.0.0',
    entry: 'bin/game-2048',
    supportedProfiles: ['youdao-y01-4.8.6'],
    supportedMachines: ['aarch64'],
    capabilities: ['storage.private'],
    limits: { memoryMiB: 32, cpuSeconds: 3600, maxFiles: 32, dataMiB: 8 },
    onlinePolicy: { mode: 'offline-v1' },
  };
}

async function fixture() {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'lvapp-test-'));
  const root = path.join(directory, 'root');
  await mkdir(path.join(root, 'bin'), { recursive: true });
  await mkdir(path.join(root, 'assets'), { recursive: true });
  await writeFile(path.join(root, 'bin', 'game-2048'), Buffer.from([0x7f, 0x45, 0x4c, 0x46, 1, 2, 3]));
  await writeFile(path.join(root, 'assets', 'theme.bin'), Buffer.from('mineral-glass'));
  const manifestPath = path.join(directory, 'app.json');
  await writeFile(manifestPath, JSON.stringify(manifest(), null, 2));
  const privateKeyPath = path.join(directory, 'private.pem');
  const publicKeyPath = path.join(directory, 'public.pem');
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  await writeFile(privateKeyPath, privateKey.export({ type: 'pkcs8', format: 'pem' }));
  await writeFile(publicKeyPath, publicKey.export({ type: 'spki', format: 'pem' }));
  return { directory, root, manifestPath, privateKeyPath, publicKeyPath };
}

test('builds deterministic dev package, signs it, and verifies Ed25519', async () => {
  const data = await fixture();
  const dev = path.join(data.directory, 'game.lvapp.dev');
  const signed = path.join(data.directory, 'game.lvapp');
  const devReport = await buildDevelopmentPackage({
    manifestPath: data.manifestPath, root: data.root, output: dev,
  });
  assert.equal(devReport.development, true);
  assert.equal(devReport.signaturePresent, false);
  assert.equal(devReport.manifest.signingKeyId, '0'.repeat(32));
  assert.deepEqual(devReport.entries.map((entry) => entry.path), [
    'assets/theme.bin', 'bin/game-2048',
  ]);
  await assert.rejects(
    verifyPackageFile({ input: dev, publicKeyPath: data.publicKeyPath }),
    (error) => error.code === 'PRODUCTION_PACKAGE_REQUIRED',
  );

  const signedReport = await signPackage({
    input: dev, keyPath: data.privateKeyPath, output: signed,
  });
  assert.equal(signedReport.development, false);
  assert.equal(signedReport.signaturePresent, true);
  assert.equal(signedReport.signatureValid, true);
  assert.notEqual(signedReport.manifest.signingKeyId, '0'.repeat(32));
  const verified = await verifyPackageFile({ input: signed, publicKeyPath: data.publicKeyPath });
  assert.equal(verified.signatureValid, true);
  assert.equal(verified.manifest.appId, 'top.lvgl.game2048');
});

test('development packages are byte-for-byte reproducible', async () => {
  const first = await fixture();
  const second = await fixture();
  const firstOut = path.join(first.directory, 'first.lvapp.dev');
  const secondOut = path.join(second.directory, 'second.lvapp.dev');
  await buildDevelopmentPackage({ manifestPath: first.manifestPath, root: first.root, output: firstOut });
  await buildDevelopmentPackage({ manifestPath: second.manifestPath, root: second.root, output: secondOut });
  assert.deepEqual(await readFile(firstOut), await readFile(secondOut));
});

test('rejects tampering, truncation, trailing bytes, and a different signer', async () => {
  const data = await fixture();
  const dev = path.join(data.directory, 'game.lvapp.dev');
  const signed = path.join(data.directory, 'game.lvapp');
  await buildDevelopmentPackage({ manifestPath: data.manifestPath, root: data.root, output: dev });
  await signPackage({ input: dev, keyPath: data.privateKeyPath, output: signed });
  const bytes = await readFile(signed);

  const tamperedPayload = Buffer.from(bytes);
  tamperedPayload[tamperedPayload.length - 97] ^= 0x01;
  assert.throws(() => parsePackage(tamperedPayload), (error) =>
    error.code === 'PACKAGE_CONTENT_HASH_INVALID');
  assert.throws(() => parsePackage(bytes.subarray(0, bytes.length - 1)), (error) =>
    ['PACKAGE_LAYOUT_INVALID', 'SIGNATURE_ENVELOPE_INVALID'].includes(error.code));
  assert.throws(() => parsePackage(Buffer.concat([bytes, Buffer.from([0])])), (error) =>
    error.code === 'PACKAGE_LAYOUT_INVALID');

  const other = generateKeyPairSync('ed25519');
  const otherKeyPath = path.join(data.directory, 'other-public.pem');
  await writeFile(otherKeyPath, other.publicKey.export({ type: 'spki', format: 'pem' }));
  await assert.rejects(
    verifyPackageFile({ input: signed, publicKeyPath: otherKeyPath }),
    (error) => error.code === 'PUBLIC_KEY_ID_MISMATCH',
  );
});

test('rejects unsafe source and package paths', async () => {
  for (const unsafe of [
    '../escape', '/absolute', 'a/../b', 'a\\b', 'a//b', 'a/./b', 'name.',
    'drive:C', 'question?', 'nul\0byte',
  ]) {
    assert.throws(() => validatePackagePath(unsafe), (error) =>
      error.code === 'PACKAGE_PATH_INVALID');
  }
  assert.equal(validatePackagePath('assets/棋盘.bin'), 'assets/棋盘.bin');
});

test('rejects non-canonical capabilities and unknown manifest fields', async () => {
  const data = await fixture();
  const source = manifest();
  source.capabilities = ['storage.private', 'haptics'];
  await writeFile(data.manifestPath, JSON.stringify(source));
  await assert.rejects(
    buildDevelopmentPackage({
      manifestPath: data.manifestPath,
      root: data.root,
      output: path.join(data.directory, 'bad.lvapp.dev'),
    }),
    (error) => error.code === 'MANIFEST_ARRAY_NON_CANONICAL',
  );

  source.capabilities = ['storage.private'];
  source.installAnyway = true;
  await writeFile(data.manifestPath, JSON.stringify(source));
  await assert.rejects(
    buildDevelopmentPackage({
      manifestPath: data.manifestPath,
      root: data.root,
      output: path.join(data.directory, 'unknown.lvapp.dev'),
    }),
    (error) => error.code === 'SOURCE_MANIFEST_KEYS_INVALID',
  );
});
