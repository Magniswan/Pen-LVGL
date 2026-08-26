#!/usr/bin/env node

import { readFile } from 'node:fs/promises';
import {
  buildDevelopmentPackage, parsePackage, publicKeyInfo, signPackage, verifyKeyProof,
  verifyPackageFile,
} from './lib.mjs';

function usage() {
  process.stderr.write([
    'Usage:',
    '  lvapp build --manifest app.json --root app-root --out app.lvapp.dev',
    '  lvapp sign --input app.lvapp.dev --key ed25519-private.pem --out app.lvapp',
    '  lvapp inspect --input app.lvapp',
    '  lvapp verify --input app.lvapp --public-key ed25519-public.pem',
    '  lvapp key-info --public-key ed25519-public.pem',
    '  lvapp verify-key-proof --challenge challenge.json --signature proof.bin --public-key ed25519-public.pem',
    '',
  ].join('\n'));
}

function options(values) {
  const result = {};
  for (let index = 0; index < values.length; index += 2) {
    const key = values[index];
    if (!key || !key.startsWith('--') || values[index + 1] === undefined) throw new Error(`invalid option: ${key || ''}`);
    result[key.slice(2)] = values[index + 1];
  }
  return result;
}

function required(value, name) {
  if (!value) throw new Error(`missing --${name}`);
  return value;
}

try {
  const [, , command, ...rest] = process.argv;
  const values = options(rest);
  let report;
  if (command === 'build') {
    report = await buildDevelopmentPackage({
      manifestPath: required(values.manifest, 'manifest'),
      root: required(values.root, 'root'),
      output: required(values.out, 'out'),
    });
  } else if (command === 'sign') {
    report = await signPackage({
      input: required(values.input, 'input'),
      keyPath: required(values.key, 'key'),
      output: required(values.out, 'out'),
    });
  } else if (command === 'inspect') {
    report = parsePackage(await readFile(required(values.input, 'input')));
  } else if (command === 'verify') {
    report = await verifyPackageFile({
      input: required(values.input, 'input'),
      publicKeyPath: required(values['public-key'], 'public-key'),
    });
  } else if (command === 'key-info') {
    report = await publicKeyInfo({
      publicKeyPath: required(values['public-key'], 'public-key'),
    });
  } else if (command === 'verify-key-proof') {
    report = await verifyKeyProof({
      challengePath: required(values.challenge, 'challenge'),
      signaturePath: required(values.signature, 'signature'),
      publicKeyPath: required(values['public-key'], 'public-key'),
    });
  } else {
    usage();
    process.exitCode = 2;
  }
  if (report) process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
} catch (error) {
  process.stderr.write(`${error.code || 'LVAPP_ERROR'}: ${error.message}\n`);
  process.exitCode = 1;
}
