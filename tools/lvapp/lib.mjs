import {
  createHash,
  createPrivateKey,
  createPublicKey,
  sign as cryptoSign,
  verify as cryptoVerify,
} from 'node:crypto';
import { lstat, mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import path from 'node:path';

export const FORMAT_VERSION = 1;
export const HEADER_SIZE = 64;
export const SIGNATURE_SIZE = 96;
export const DEV_FLAG = 1;
export const MAX_PACKAGE_SIZE = 256 * 1024 * 1024;
export const MAX_MANIFEST_SIZE = 64 * 1024;
export const MAX_FILES = 256;
export const MAX_PATH_BYTES = 240;

const PACKAGE_MAGIC = Buffer.from('LVAPP001', 'ascii');
const SIGNATURE_MAGIC = Buffer.from('LVSIG001', 'ascii');
const SIGNATURE_DOMAIN = Buffer.alloc(16);
SIGNATURE_DOMAIN.write('LVAPP-SIGN-V1', 'ascii');
const textDecoder = new TextDecoder('utf-8', { fatal: true });
const zeroKeyId = '0'.repeat(32);
const allowedCapabilities = new Set([
  'audio.output', 'dictionary.lookup', 'haptics', 'microphone', 'network',
  'scanner', 'storage.private',
]);

function fail(code, message) {
  const error = new Error(message);
  error.code = code;
  throw error;
}

function exactKeys(value, keys, code) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    fail(code, 'expected an object');
  }
  const actual = Object.keys(value).sort();
  const expected = [...keys].sort();
  if (actual.length !== expected.length || actual.some((key, index) => key !== expected[index])) {
    fail(code, `unexpected or missing keys: expected ${expected.join(', ')}`);
  }
}

function safeInteger(value, name, minimum, maximum) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    fail('MANIFEST_INTEGER_INVALID', `${name} is outside the allowed range`);
  }
  return value;
}

function safeString(value, name, maximum = 255) {
  if (typeof value !== 'string' || value.length === 0 || value.length > maximum || value.includes('\0')) {
    fail('MANIFEST_STRING_INVALID', `${name} is invalid`);
  }
  return value;
}

function sortedUniqueStrings(value, name, maximum = 64) {
  if (!Array.isArray(value) || value.length === 0 || value.length > maximum) {
    fail('MANIFEST_ARRAY_INVALID', `${name} is invalid`);
  }
  const items = value.map((item, index) => safeString(item, `${name}[${index}]`));
  const sorted = [...items].sort((left, right) => Buffer.compare(Buffer.from(left), Buffer.from(right)));
  if (items.some((item, index) => item !== sorted[index]) || new Set(items).size !== items.length) {
    fail('MANIFEST_ARRAY_NON_CANONICAL', `${name} must be sorted and unique`);
  }
  return items;
}

export function canonicalJson(value) {
  if (value === null || typeof value === 'boolean' || typeof value === 'string') return JSON.stringify(value);
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) fail('JSON_NUMBER_INVALID', 'canonical JSON permits safe integers only');
    return String(value);
  }
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(',')}]`;
  if (value && typeof value === 'object') {
    const keys = Object.keys(value).sort((left, right) => Buffer.compare(Buffer.from(left), Buffer.from(right)));
    return `{${keys.map((key) => `${JSON.stringify(key)}:${canonicalJson(value[key])}`).join(',')}}`;
  }
  fail('JSON_TYPE_INVALID', 'canonical JSON contains an unsupported type');
}

export function validatePackagePath(value) {
  if (typeof value !== 'string' || value.length === 0 || value.length > MAX_PATH_BYTES ||
      value.includes('\0')) {
    fail('PACKAGE_PATH_INVALID', 'package path is invalid');
  }
  const encoded = Buffer.from(value, 'utf8');
  if (encoded.length > MAX_PATH_BYTES || textDecoder.decode(encoded) !== value || value.startsWith('/') ||
      value.includes('\\') || value.endsWith('/') || value.includes('//')) {
    fail('PACKAGE_PATH_INVALID', `unsafe package path: ${value}`);
  }
  const segments = value.split('/');
  if (segments.some((segment) => segment === '' || segment === '.' || segment === '..' ||
      segment.endsWith('.') || /[\x00-\x1f\x7f:*?"<>|]/u.test(segment))) {
    fail('PACKAGE_PATH_INVALID', `unsafe package path: ${value}`);
  }
  return value;
}

function validateManifest(manifest, { development }) {
  exactKeys(manifest, [
    'appId', 'capabilities', 'entry', 'files', 'formatVersion', 'limits',
    'minPlatformVersion', 'name', 'onlinePolicy', 'releaseCounter', 'sdkAbi',
    'securityEpoch', 'signingKeyId', 'supportedMachines', 'supportedProfiles', 'version',
  ], 'MANIFEST_KEYS_INVALID');
  if (manifest.formatVersion !== FORMAT_VERSION) fail('MANIFEST_FORMAT_INVALID', 'unsupported manifest format');
  if (!/^[a-z0-9]+(?:[.-][a-z0-9]+)+$/u.test(safeString(manifest.appId, 'appId', 96))) {
    fail('MANIFEST_APP_ID_INVALID', 'appId must be a lowercase reverse-domain identifier');
  }
  safeString(manifest.name, 'name', 64);
  if (!/^\d+\.\d+\.\d+$/u.test(safeString(manifest.version, 'version', 32)) ||
      !/^\d+\.\d+\.\d+$/u.test(safeString(manifest.minPlatformVersion, 'minPlatformVersion', 32))) {
    fail('MANIFEST_VERSION_INVALID', 'versions must use numeric semantic versioning');
  }
  safeInteger(manifest.releaseCounter, 'releaseCounter', 1, Number.MAX_SAFE_INTEGER);
  safeInteger(manifest.securityEpoch, 'securityEpoch', 0, 0xffffffff);
  if (manifest.sdkAbi !== '1.0') fail('MANIFEST_SDK_ABI_INVALID', 'unsupported SDK ABI');
  validatePackagePath(manifest.entry);
  sortedUniqueStrings(manifest.supportedProfiles, 'supportedProfiles');
  sortedUniqueStrings(manifest.supportedMachines, 'supportedMachines');
  sortedUniqueStrings(manifest.capabilities.length ? manifest.capabilities : ['storage.private'], 'capabilities');
  if (manifest.capabilities.length === 0) manifest.capabilities = [];
  for (const capability of manifest.capabilities) {
    if (!allowedCapabilities.has(capability)) fail('MANIFEST_CAPABILITY_INVALID', `unsupported capability: ${capability}`);
  }
  exactKeys(manifest.limits, ['cpuSeconds', 'dataMiB', 'maxFiles', 'memoryMiB'], 'MANIFEST_LIMITS_INVALID');
  safeInteger(manifest.limits.memoryMiB, 'limits.memoryMiB', 8, 512);
  safeInteger(manifest.limits.cpuSeconds, 'limits.cpuSeconds', 1, 86400);
  safeInteger(manifest.limits.maxFiles, 'limits.maxFiles', 1, MAX_FILES);
  safeInteger(manifest.limits.dataMiB, 'limits.dataMiB', 1, 4096);
  exactKeys(manifest.onlinePolicy, ['mode'], 'MANIFEST_ONLINE_POLICY_INVALID');
  if (manifest.onlinePolicy.mode !== 'offline-v1') fail('MANIFEST_ONLINE_POLICY_INVALID', 'unsupported online policy');
  if (!/^[0-9a-f]{32}$/u.test(manifest.signingKeyId) ||
      (development ? manifest.signingKeyId !== zeroKeyId : manifest.signingKeyId === zeroKeyId)) {
    fail('MANIFEST_KEY_ID_INVALID', 'signing key ID is invalid for this package type');
  }
  if (!Array.isArray(manifest.files) || manifest.files.length === 0 || manifest.files.length > MAX_FILES) {
    fail('MANIFEST_FILES_INVALID', 'manifest files list is invalid');
  }
  let previous = '';
  let hasEntry = false;
  for (const [index, file] of manifest.files.entries()) {
    exactKeys(file, ['mode', 'path', 'role', 'sha256', 'size'], 'MANIFEST_FILE_KEYS_INVALID');
    validatePackagePath(file.path);
    if (index > 0 && Buffer.compare(Buffer.from(previous), Buffer.from(file.path)) >= 0) {
      fail('MANIFEST_FILES_NON_CANONICAL', 'manifest files must be sorted and unique');
    }
    previous = file.path;
    safeInteger(file.size, `files[${index}].size`, 0, MAX_PACKAGE_SIZE);
    if (file.mode !== 0o644 && file.mode !== 0o755) fail('MANIFEST_FILE_MODE_INVALID', 'invalid file mode');
    if (file.role !== 'asset' && file.role !== 'executable') fail('MANIFEST_FILE_ROLE_INVALID', 'invalid file role');
    if (!/^[0-9a-f]{64}$/u.test(file.sha256)) fail('MANIFEST_FILE_HASH_INVALID', 'invalid SHA-256');
    if (file.path === manifest.entry) {
      hasEntry = file.role === 'executable' && file.mode === 0o755;
    }
  }
  if (!hasEntry) fail('MANIFEST_ENTRY_INVALID', 'entry must name one executable file');
}

async function collectFiles(root) {
  const rootStat = await lstat(root);
  if (!rootStat.isDirectory() || rootStat.isSymbolicLink()) fail('SOURCE_ROOT_INVALID', 'source root must be a real directory');
  const files = [];
  async function walk(directory, prefix) {
    const names = await readdir(directory);
    names.sort((left, right) => Buffer.compare(Buffer.from(left), Buffer.from(right)));
    for (const name of names) {
      const relative = prefix ? `${prefix}/${name}` : name;
      validatePackagePath(relative);
      const absolute = path.join(directory, name);
      const stat = await lstat(absolute);
      if (stat.isSymbolicLink()) fail('SOURCE_LINK_REJECTED', `links are not permitted: ${relative}`);
      if (stat.isDirectory()) {
        await walk(absolute, relative);
      } else if (stat.isFile()) {
        files.push({ relative, absolute, stat });
      } else {
        fail('SOURCE_TYPE_REJECTED', `non-regular source entry: ${relative}`);
      }
      if (files.length > MAX_FILES) fail('SOURCE_FILE_COUNT_EXCEEDED', 'source contains too many files');
    }
  }
  await walk(root, '');
  return files;
}

function setHeader(buffer, fields) {
  PACKAGE_MAGIC.copy(buffer, 0);
  buffer.writeUInt16LE(FORMAT_VERSION, 8);
  buffer.writeUInt16LE(HEADER_SIZE, 10);
  buffer.writeUInt32LE(fields.flags, 12);
  buffer.writeUInt32LE(fields.manifestSize, 16);
  buffer.writeUInt32LE(fields.entryCount, 20);
  buffer.writeBigUInt64LE(BigInt(fields.tableSize), 24);
  buffer.writeBigUInt64LE(BigInt(fields.payloadSize), 32);
  buffer.writeBigUInt64LE(BigInt(fields.unsignedSize), 40);
  buffer.writeBigUInt64LE(BigInt(fields.totalSize), 48);
  buffer.writeUInt32LE(1, 56);
  buffer.writeUInt32LE(0, 60);
}

function readSafeU64(buffer, offset, name) {
  const value = buffer.readBigUInt64LE(offset);
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) fail('PACKAGE_LENGTH_OVERFLOW', `${name} is too large`);
  return Number(value);
}

function publicKeyId(publicKey) {
  const der = publicKey.export({ type: 'spki', format: 'der' });
  return createHash('sha256').update(der).digest().subarray(0, 16);
}

function loadPrivateKey(pem) {
  const key = createPrivateKey(pem);
  if (key.asymmetricKeyType !== 'ed25519') fail('SIGNING_KEY_INVALID', 'signing key must be Ed25519');
  return key;
}

function loadPublicKey(pem) {
  const key = createPublicKey(pem);
  if (key.asymmetricKeyType !== 'ed25519') fail('PUBLIC_KEY_INVALID', 'public key must be Ed25519');
  return key;
}

export async function buildDevelopmentPackage({ manifestPath, root, output }) {
  const sourceManifest = JSON.parse(await readFile(manifestPath, 'utf8'));
  exactKeys(sourceManifest, [
    'appId', 'capabilities', 'entry', 'formatVersion', 'limits', 'minPlatformVersion',
    'name', 'onlinePolicy', 'releaseCounter', 'sdkAbi', 'securityEpoch',
    'supportedMachines', 'supportedProfiles', 'version',
  ], 'SOURCE_MANIFEST_KEYS_INVALID');
  const sourceFiles = await collectFiles(root);
  if (sourceFiles.length === 0) fail('SOURCE_EMPTY', 'source root contains no files');

  const entries = [];
  for (const file of sourceFiles) {
    const content = await readFile(file.absolute);
    entries.push({
      path: file.relative,
      mode: file.relative === sourceManifest.entry ? 0o755 : 0o644,
      role: file.relative === sourceManifest.entry ? 'executable' : 'asset',
      content,
      sha256: createHash('sha256').update(content).digest(),
    });
  }
  entries.sort((left, right) => Buffer.compare(Buffer.from(left.path), Buffer.from(right.path)));
  const manifest = {
    ...sourceManifest,
    signingKeyId: zeroKeyId,
    files: entries.map((entry) => ({
      mode: entry.mode,
      path: entry.path,
      role: entry.role,
      sha256: entry.sha256.toString('hex'),
      size: entry.content.length,
    })),
  };
  validateManifest(manifest, { development: true });
  const manifestBytes = Buffer.from(canonicalJson(manifest), 'utf8');
  if (manifestBytes.length > MAX_MANIFEST_SIZE) fail('MANIFEST_TOO_LARGE', 'manifest is too large');

  let payloadSize = 0;
  const tableParts = [];
  for (const entry of entries) {
    const pathBytes = Buffer.from(entry.path, 'utf8');
    const record = Buffer.alloc(56 + pathBytes.length);
    record.writeUInt16LE(pathBytes.length, 0);
    record.writeUInt16LE(entry.mode, 2);
    record.writeUInt32LE(0, 4);
    record.writeBigUInt64LE(BigInt(entry.content.length), 8);
    record.writeBigUInt64LE(BigInt(payloadSize), 16);
    entry.sha256.copy(record, 24);
    pathBytes.copy(record, 56);
    tableParts.push(record);
    payloadSize += entry.content.length;
  }
  const table = Buffer.concat(tableParts);
  const unsignedSize = HEADER_SIZE + manifestBytes.length + table.length + payloadSize;
  if (unsignedSize > MAX_PACKAGE_SIZE) fail('PACKAGE_TOO_LARGE', 'package exceeds the size limit');
  const header = Buffer.alloc(HEADER_SIZE);
  setHeader(header, {
    flags: DEV_FLAG,
    manifestSize: manifestBytes.length,
    entryCount: entries.length,
    tableSize: table.length,
    payloadSize,
    unsignedSize,
    totalSize: unsignedSize,
  });
  const packageBytes = Buffer.concat([header, manifestBytes, table, ...entries.map((entry) => entry.content)]);
  await mkdir(path.dirname(output), { recursive: true });
  await writeFile(output, packageBytes, { flag: 'wx' });
  return parsePackage(packageBytes);
}

export async function signPackage({ input, keyPath, output }) {
  const developmentBytes = await readFile(input);
  const parsed = parsePackage(developmentBytes);
  if (!parsed.development) fail('SIGN_INPUT_NOT_DEVELOPMENT', 'only a development package can be signed');
  const privateKey = loadPrivateKey(await readFile(keyPath));
  const publicKey = createPublicKey(privateKey);
  const keyId = publicKeyId(publicKey);
  const manifest = { ...parsed.manifest, signingKeyId: keyId.toString('hex') };
  validateManifest(manifest, { development: false });
  const manifestBytes = Buffer.from(canonicalJson(manifest), 'utf8');
  if (manifestBytes.length !== parsed.manifestSize) fail('SIGN_MANIFEST_SIZE_CHANGED', 'key substitution changed manifest size');

  const unsigned = Buffer.from(developmentBytes);
  unsigned.writeUInt32LE(0, 12);
  unsigned.writeBigUInt64LE(BigInt(unsigned.length + SIGNATURE_SIZE), 48);
  manifestBytes.copy(unsigned, HEADER_SIZE);
  const digest = createHash('sha512').update(unsigned).digest();
  const signature = cryptoSign(null, Buffer.concat([SIGNATURE_DOMAIN, digest]), privateKey);
  if (signature.length !== 64) fail('SIGNATURE_LENGTH_INVALID', 'unexpected Ed25519 signature length');
  const envelope = Buffer.alloc(SIGNATURE_SIZE);
  SIGNATURE_MAGIC.copy(envelope, 0);
  envelope.writeUInt16LE(1, 8);
  envelope.writeUInt16LE(1, 10);
  keyId.copy(envelope, 12);
  signature.copy(envelope, 28);
  envelope.writeUInt32LE(0, 92);
  const signed = Buffer.concat([unsigned, envelope]);
  await mkdir(path.dirname(output), { recursive: true });
  await writeFile(output, signed, { flag: 'wx' });
  return parsePackage(signed, { publicKey });
}

export function parsePackage(bytes, { publicKey } = {}) {
  if (!Buffer.isBuffer(bytes)) bytes = Buffer.from(bytes);
  if (bytes.length < HEADER_SIZE || bytes.length > MAX_PACKAGE_SIZE + SIGNATURE_SIZE) {
    fail('PACKAGE_SIZE_INVALID', 'package length is outside the allowed range');
  }
  if (!bytes.subarray(0, 8).equals(PACKAGE_MAGIC) || bytes.readUInt16LE(8) !== FORMAT_VERSION ||
      bytes.readUInt16LE(10) !== HEADER_SIZE) fail('PACKAGE_HEADER_INVALID', 'package header is invalid');
  const flags = bytes.readUInt32LE(12);
  if (flags !== 0 && flags !== DEV_FLAG) fail('PACKAGE_FLAGS_INVALID', 'package contains unsupported flags');
  const development = flags === DEV_FLAG;
  const manifestSize = bytes.readUInt32LE(16);
  const entryCount = bytes.readUInt32LE(20);
  const tableSize = readSafeU64(bytes, 24, 'tableSize');
  const payloadSize = readSafeU64(bytes, 32, 'payloadSize');
  const unsignedSize = readSafeU64(bytes, 40, 'unsignedSize');
  const totalSize = readSafeU64(bytes, 48, 'totalSize');
  if (bytes.readUInt32LE(56) !== 1 || bytes.readUInt32LE(60) !== 0 ||
      manifestSize === 0 || manifestSize > MAX_MANIFEST_SIZE || entryCount === 0 ||
      entryCount > MAX_FILES || totalSize !== bytes.length ||
      unsignedSize !== HEADER_SIZE + manifestSize + tableSize + payloadSize ||
      totalSize !== unsignedSize + (development ? 0 : SIGNATURE_SIZE)) {
    fail('PACKAGE_LAYOUT_INVALID', 'package layout is inconsistent');
  }

  const manifestStart = HEADER_SIZE;
  const tableStart = manifestStart + manifestSize;
  const payloadStart = tableStart + tableSize;
  const manifestText = textDecoder.decode(bytes.subarray(manifestStart, tableStart));
  const manifest = JSON.parse(manifestText);
  if (canonicalJson(manifest) !== manifestText) fail('MANIFEST_NON_CANONICAL', 'manifest is not canonical JSON');
  validateManifest(manifest, { development });

  const entries = [];
  let cursor = tableStart;
  let expectedOffset = 0;
  let previousPath = '';
  for (let index = 0; index < entryCount; index += 1) {
    if (cursor + 56 > payloadStart) fail('PACKAGE_TABLE_TRUNCATED', 'file table is truncated');
    const pathSize = bytes.readUInt16LE(cursor);
    const mode = bytes.readUInt16LE(cursor + 2);
    const entryFlags = bytes.readUInt32LE(cursor + 4);
    const size = readSafeU64(bytes, cursor + 8, 'entrySize');
    const offset = readSafeU64(bytes, cursor + 16, 'entryOffset');
    const sha256 = bytes.subarray(cursor + 24, cursor + 56);
    cursor += 56;
    if (pathSize === 0 || pathSize > MAX_PATH_BYTES || cursor + pathSize > payloadStart ||
        entryFlags !== 0 || (mode !== 0o644 && mode !== 0o755) || offset !== expectedOffset ||
        size > payloadSize - offset) fail('PACKAGE_ENTRY_INVALID', 'file table entry is invalid');
    const packagePath = textDecoder.decode(bytes.subarray(cursor, cursor + pathSize));
    validatePackagePath(packagePath);
    if (previousPath && Buffer.compare(Buffer.from(previousPath), Buffer.from(packagePath)) >= 0) {
      fail('PACKAGE_ENTRY_ORDER_INVALID', 'file table is not sorted and unique');
    }
    cursor += pathSize;
    const content = bytes.subarray(payloadStart + offset, payloadStart + offset + size);
    if (!createHash('sha256').update(content).digest().equals(sha256)) {
      fail('PACKAGE_CONTENT_HASH_INVALID', `content hash failed for ${packagePath}`);
    }
    entries.push({ path: packagePath, mode, size, offset, sha256: sha256.toString('hex') });
    previousPath = packagePath;
    expectedOffset += size;
  }
  if (cursor !== payloadStart || expectedOffset !== payloadSize || manifest.files.length !== entries.length) {
    fail('PACKAGE_TABLE_SIZE_INVALID', 'file table or payload has unaccounted bytes');
  }
  for (let index = 0; index < entries.length; index += 1) {
    const tableEntry = entries[index];
    const manifestEntry = manifest.files[index];
    if (tableEntry.path !== manifestEntry.path || tableEntry.mode !== manifestEntry.mode ||
        tableEntry.size !== manifestEntry.size || tableEntry.sha256 !== manifestEntry.sha256) {
      fail('PACKAGE_MANIFEST_MISMATCH', 'manifest and file table differ');
    }
  }

  let signature = null;
  let signatureValid = false;
  if (!development) {
    const envelope = bytes.subarray(unsignedSize);
    if (!envelope.subarray(0, 8).equals(SIGNATURE_MAGIC) || envelope.readUInt16LE(8) !== 1 ||
        envelope.readUInt16LE(10) !== 1 || envelope.readUInt32LE(92) !== 0) {
      fail('SIGNATURE_ENVELOPE_INVALID', 'signature envelope is invalid');
    }
    const keyId = envelope.subarray(12, 28);
    signature = envelope.subarray(28, 92);
    if (keyId.toString('hex') !== manifest.signingKeyId) fail('SIGNATURE_KEY_ID_MISMATCH', 'signature key IDs differ');
    if (publicKey) {
      const normalizedPublicKey = publicKey.type === 'public' ? publicKey : loadPublicKey(publicKey);
      if (!publicKeyId(normalizedPublicKey).equals(keyId)) fail('PUBLIC_KEY_ID_MISMATCH', 'public key is not the package signer');
      const digest = createHash('sha512').update(bytes.subarray(0, unsignedSize)).digest();
      signatureValid = cryptoVerify(null, Buffer.concat([SIGNATURE_DOMAIN, digest]), normalizedPublicKey, signature);
      if (!signatureValid) fail('SIGNATURE_INVALID', 'Ed25519 signature verification failed');
    }
  }
  return {
    development,
    entryCount,
    entries,
    manifest,
    manifestSize,
    packageSha256: createHash('sha256').update(bytes).digest('hex'),
    signaturePresent: !development,
    signatureValid,
    totalSize,
    unsignedSize,
  };
}

export async function verifyPackageFile({ input, publicKeyPath }) {
  const publicKey = publicKeyPath ? loadPublicKey(await readFile(publicKeyPath)) : undefined;
  const parsed = parsePackage(await readFile(input), { publicKey });
  if (parsed.development) fail('PRODUCTION_PACKAGE_REQUIRED', 'development packages are never installable');
  if (!publicKey) fail('PUBLIC_KEY_REQUIRED', 'verification requires an explicit public key');
  return parsed;
}

export async function publicKeyInfo({ publicKeyPath }) {
  const publicKey = loadPublicKey(await readFile(publicKeyPath));
  return publicKeyReport(publicKey);
}

function publicKeyReport(publicKey) {
  const jwk = publicKey.export({ format: 'jwk' });
  if (typeof jwk.x !== 'string') fail('PUBLIC_KEY_EXPORT_INVALID', 'Ed25519 public key has no raw coordinate');
  const raw = Buffer.from(jwk.x, 'base64url');
  if (raw.length !== 32) fail('PUBLIC_KEY_EXPORT_INVALID', 'Ed25519 public key must contain 32 raw bytes');
  return {
    algorithm: 'Ed25519',
    keyId: publicKeyId(publicKey).toString('hex'),
    rawPublicKeyHex: raw.toString('hex'),
  };
}

export async function verifyKeyProof({ challengePath, signaturePath, publicKeyPath }) {
  const [challenge, signature, pem] = await Promise.all([
    readFile(challengePath), readFile(signaturePath), readFile(publicKeyPath),
  ]);
  if (signature.length !== 64) fail('KEY_PROOF_LENGTH_INVALID', 'Ed25519 key proof must be exactly 64 bytes');
  const publicKey = loadPublicKey(pem);
  if (!cryptoVerify(null, challenge, publicKey, signature)) {
    fail('KEY_PROOF_INVALID', 'Ed25519 key proof does not authenticate the exact challenge bytes');
  }
  return {
    ...publicKeyReport(publicKey),
    challengeSha512: createHash('sha512').update(challenge).digest('hex'),
    proofValid: true,
  };
}
