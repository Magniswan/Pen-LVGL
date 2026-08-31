# Personal Unbound LVGL Release Design

**Status:** User-approved design; implementation not started.

**Date:** 2026-08-31

## Objective

Make the LVGL platform usable on the owner's AArch64 dictionary-pen devices
without per-device identity, model, or certified-profile binding.  The first
release is personal-only: it retains one official Ed25519 trust root and signed
package verification, but deliberately does not claim commercial hardware
certification or hostile-root resistance.

## Decisions

- The owner keeps one plaintext Ed25519 private key outside the repository at
  `C:\Users\sunho\.lvgl-official-signer\private\official-ed25519-private.pem`.
- The private-key directory is protected with NTFS ACLs for the current Windows
  account and `SYSTEM`; it is not copied to the repository, an AMR, a device,
  logs, or automatic backups.
- The public key is the only trust root compiled into the generic manager and
  used to verify platform and application packages.
- The manager is not bound to ADB serials, device identity digests, model names,
  firmware strings, or a pre-approved Profile.  It accepts only its supported
  AArch64 ABI plus a package authenticated by the one official public key.
- The platform derives screen, touch, and DRM configuration from the live device
  at start-up.  A missing required display or input capability is an operational
  error, not an identity-policy rejection.
- LVGL applications run directly as root.  The personal release intentionally
  does not enforce cgroup v2, mount namespace, RLIMIT, seccomp, UID/GID drop, or
  brokered device-handle restrictions.
- Unsigned packages, malformed packages, packages signed by another key, and
  invalid package hashes remain rejected.  There is no install-anyway path.
- No public distribution, mandatory update service, automatic update, or claim
  of commercial certification is included in this release.

## Components

### Personal signer

New local tooling generates an Ed25519 pair using locked Node 18.20.8 and
writes the private key only to the external signer directory.  It emits a public
evidence bundle containing the public PEM, raw public-key hex, key ID,
proof-of-possession, checksums, source commit, and an explicit
`plaintext-local-single-operator` protection declaration.  The bundle contains
no private-key bytes.

Signing accepts an approved development package digest and source commit, emits
a signed `.lvapp`, and immediately verifies it using only the public key.  The
release audit records package hashes and key ID without recording the private
key or its path.

### Generic manager AMR

The manager build embeds the public key and the exact signed platform payload.
It removes the current certified-device identity gate and profile/machine
allow-list requirement.  Installation still verifies the signed envelope,
platform application ID, entrypoint, SDK ABI, release counter, and all package
file hashes before atomically activating the release.

### Runtime and launcher

`lvgl-sessiond` starts a verified official application directly as root.  It
does not create a cgroup, unshare a mount namespace, alter resource limits,
install seccomp, change UID/GID, or proxy hardware descriptors.  It retains
exact-child tracking, explicit stop/reap handling, and does not signal Falcon or
the `miniapp` process.

At startup the runtime reads the device's available display, touch, and DRM
facts.  The launcher presents a clear error when those capabilities cannot form
a working session; it does not use a hard-coded model or device fingerprint.

## Data Flow

1. The owner builds a deterministic platform development package.
2. The external signer signs exactly that package with the personal official
   key and public-key-only verification confirms the resulting `.lvapp`.
3. The generic manager AMR is built with the public key and that signed payload.
4. Falcon installs the manager and launcher AMRs.
5. Manager verifies and installs the embedded signed platform transaction.
6. Launcher asks the platform to start; the platform probes live display/touch
   capabilities and runs the selected official LVGL application as root.

## Error Handling

- Missing private key, wrong Node version, invalid ACL, dirty source, malformed
  proof, or a package/public-key mismatch stops signing before output is
  published.
- An unsigned, malformed, hash-mismatched, wrong-ID, or other-key-signed
  package is rejected before installation.
- A device without AArch64 support, a usable display, or usable touch reports a
  precise launch error and starts no LVGL application.
- Repeated launch or stop requests act only on the exact tracked child; failure
  to reap the child is reported as a session error and never escalates to
  `killall`, `pkill`, Falcon termination, or device reboot.

## Verification

- Unit tests cover signer key isolation, proof generation/verification, package
  rejection for a wrong key or modified byte, and generic-manager removal of
  device/profile gates.
- Build checks use Node 18.20.8, inspect AMR manifests and native ELF headers,
  and verify that no private key is present under the repository or artifacts.
- Real-device checks install manager and launcher, cold-start both with the
  firmware's explicit `index` page parameter, install the signed platform,
  start the launcher, verify display and touch, perform two enter/exit cycles,
  and confirm no untracked LVGL process remains.
- The result is labelled personal compatibility validation, not multi-device or
  commercial certification.

## Security and Operational Limits

The plaintext key is vulnerable to compromise of the owner's Windows account,
disk, backup, or process memory.  A root-level application is trusted solely
because it carries the owner's official signature and can modify the system
without sandbox containment.  A compromised device root or kernel can bypass
user-space verification.  A later commercial release must introduce an
HSM-backed key, a profile compatibility policy, and the desired isolation
controls as a new security epoch; it must not silently relabel this release as
certified.
