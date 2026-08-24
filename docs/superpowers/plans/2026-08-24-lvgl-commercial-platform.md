# Commercial LVGL Platform Implementation Plan

Date: 2026-08-24

Design: `docs/superpowers/specs/2026-08-24-lvgl-commercial-platform-design.md`

## Working Rules

- Preserve unrelated work and commit each independently verifiable milestone.
- Production builds never contain a private or test signing key.
- Security-negative tests are implemented with each parser/policy change.
- Hardware-specific behavior is profile-driven and remains unclaimed until a
  real device completes certification.
- Existing PoC code is migrated behind stable interfaces; no second display or
  lifecycle stack is created.

## Milestone 1: Repository and Contract Foundation

Files:

- update `CMakeLists.txt`
- add `platform/include/lvgl_platform/*.h`
- add `platform/src/common/`, `platform/src/ipc/`, `platform/src/profile/`
- add `profiles/schema.json`, `profiles/y01-4.8.6.json`
- add `tests/host/`

Work:

1. Define stable error domains, version constants, application IDs, paths,
   process states, lifecycle events, and resource-limit structures.
2. Define the bounded launcher/session IPC frame and touch coordinate contract.
3. Convert the current Y01 constants into an explicit versioned profile.
4. Add native host-test targets and CTest integration independent of LVGL/DRM.
5. Test profile exact matching, ambiguity rejection, coordinate corners,
   malformed IPC, sequence rejection, and protocol-version mismatch.

Verification:

- host configure and build succeed without the device sysroot;
- `ctest` passes contract/profile/IPC tests;
- searches show no new model, plane, rotation, or screen constants in apps.

## Milestone 2: `.lvapp` Format and Offline Trust Root

Files:

- add `platform/src/crypto/sha256.*`, `platform/src/crypto/ed25519.*`
- add `platform/src/package/package_reader.*`
- add `platform/src/package/manifest.*`
- add `tools/lvapp/*.js`, `tools/lvapp/package.json`
- add `tests/host/package_*_test.cpp`
- add `tests/vectors/lvapp/`

Work:

1. Implement the deterministic streaming package format with bounded fixed
   headers, canonical manifest bytes, regular-file table, contents, and an
   Ed25519 signature envelope.
2. Implement host `build`, `sign`, `inspect`, and `verify` commands using locked
   Node 18 and built-in cryptography. Signing accepts a PEM path only through an
   explicit release command and never prints key material.
3. Implement native verification with a portable verifier or a fail-closed,
   profile-declared crypto provider and known Ed25519 test vectors.
4. Compile the production public-key allowlist and test-key denylist into one
   generated header consumed by every production verifier.
5. Reject path traversal, links, duplicates, invalid UTF-8, oversized fields,
   length overflow, overlap, truncation, trailing bytes, unknown critical
   flags, altered content, wrong keys, and non-canonical manifests.
6. Add a developer artifact suffix that production verifiers reject.

Verification:

- positive and negative host vectors pass in Node and native implementations;
- a byte changed at every package region fails verification;
- test keys are rejected by a production-mode verifier;
- private-key and credential scans are clean.

## Milestone 3: Transactional Store and Anti-Rollback

Files:

- add `platform/src/storage/safe_path.*`, `atomic_file.*`
- add `platform/src/package/package_installer.*`, `rollback_policy.*`
- add `platform/src/trust/trust_store.*`
- add `tests/host/installer_*_test.cpp`

Work:

1. Implement no-follow contained traversal, safe directory ownership checks,
   regular-file-only extraction, fsync ordering, versioned staging, and atomic
   activation.
2. Store current/previous/quarantined releases and redundant high-water marks.
3. Enforce application-ID ownership, SDK ABI, platform version, profile/ABI,
   security epoch, release counter, capability allowlist, size, and free-space
   policies.
4. Reverify installed files before each launch and expose a stable inspection
   report to the installer UI.
5. Make install/repair/remove idempotent and power-interruption recoverable.

Verification:

- filesystem-fixture tests cover interruption at every commit boundary;
- symlink/race fixtures never escape the temporary root;
- downgrade, package-ID takeover, partial release, and corrupted rollback fail
  closed;
- previous verified release is restored after simulated first-launch failure.

## Milestone 4: Falcon Platform Manager AMR

Files:

- add `falcon/manager/package.json`, locked dependencies, app/page sources
- add `falcon/manager/native/`
- add `falcon/manager/test/`
- add `scripts/build_manager.*`, `scripts/install_manager.*`

Work:

1. Build a four-state Install/Repair/Upgrade/Remove Falcon UI.
2. Embed a signed, profile-indexed platform payload and its release metadata.
3. Expose only fixed native JSAPI operations and stable progress/result events.
4. Enforce exact device/profile preflight and owned-path deletion.
5. Keep the manager installed after platform removal so reinstall remains
   possible; remove only the launcher AMR and native platform tree.

Verification:

- JS state-machine and source-contract tests pass;
- native argument and owned-path tests pass;
- AMR uses the locked Falcon builder and contains exactly the expected files;
- simulator proves UI states, while real-device results remain profile-bound.

## Milestone 5: Persistent Hole Launcher and Touch IPC

Files:

- migrate `launcher/` to `falcon/launcher/`
- update launcher Vue page, lifecycle service, native JSAPI, and mocks
- add `platform/src/session/session_daemon.*`, `touch_router.*`
- add `tests/host/session_*_test.cpp`

Work:

1. Replace Falcon-killing supervisor behavior with a persistent full-screen
   `<hole>` and transparent touch layer based on CloudBrowser's proven pattern.
2. Native JSAPI starts/stops exactly one session daemon and exposes status
   events; it accepts no arbitrary executable, command, or shell text.
3. Normalize multi-touch Falcon events, coalesce moves, preserve boundaries,
   attach a session nonce/sequence, and reject stale frames.
4. Make page hide/unload, native exit, IPC failure, and cancellation converge
   on one idempotent stop path.
5. Select the display plane only from a certified profile and refuse fallback
   to killing or stopping Falcon.

Verification:

- launcher lifecycle/mocks and native protocol tests pass;
- source checks prove no `killall`, broad `pkill`, guardian stop, or miniapp stop;
- device certification checks hole composition, touch corners, repeated entry,
  and Falcon continuity when the correct pen is available.

## Milestone 6: Session Isolation, SDK, and Desktop

Files:

- add `platform/src/runtime/application_supervisor.*`, `sandbox.*`
- add `sdk/include/`, `sdk/cmake/`, `sdk/template/`
- migrate shell/runtime code behind the SDK
- add `apps/desktop/`
- add `docs/development/`, `docs/security/`, `docs/device-porting/`

Work:

1. Implement the application lifecycle, foreground ownership, watchdog,
   exact-PID stop/reap, resource limits, environment clearing, descriptor
   closing, parent-death handling, and profile-gated isolation features.
2. Expose display, input, theme, navigation, timer, storage, safe logging, and
   settings services through versioned SDK interfaces.
3. Create the SDK template, host simulator adapter, mocks, asset/font pipeline,
   CMake helpers, development packaging, and contract tests.
4. Build the horizontal precision-instrument desktop, verified seal, app detail,
   launch/return/crash/quarantine states, reduced motion, and low-power policy.

Verification:

- lifecycle, crash, hang, resource, state-isolation, and ABI contract tests pass;
- a generated template app builds and produces a verifiable developer package;
- desktop build and screenshot review pass at all fixture profiles.

## Milestone 7: On-Device Application Installer

Files:

- add `apps/installer/`
- add `platform/src/services/inbox_service.*`
- add installer UI/model tests

Work:

1. Implement inbox scanning without following links or trusting filenames.
2. Display signed identity, version, publisher root, permissions, compatibility,
   size, and verification status.
3. Implement Inspect/Verify/Install/Result, upgrade, remove, quarantine, and
   rollback UI with no bypass path.
4. Refresh the desktop registry atomically after a successful transaction.

Verification:

- every invalid package class maps to a stable, actionable failure;
- no UI or protocol route installs an unverified package;
- interrupted install and removal retain a recoverable platform.

## Milestone 8: Polished 2048 Reference Application

Files:

- add `apps/game-2048/model.*`, `animation.*`, `ui.*`, `main.cpp`
- add `tests/host/game_2048_*_test.cpp`
- add `docs/development/2048-reference-walkthrough.md`

Work:

1. Implement deterministic model logic, seeded spawning, scoring, win/lose,
   persisted best score, one-move undo, and restart confirmation.
2. Implement wide-screen responsive layout and mineral-glass visual tokens.
3. Add translation, merge pulse, halo, score fly-up, first-entry choreography,
   one-event input queue, reduced motion, and idle animation suspension.
4. Package the application through the official signing handoff and use it as
   the SDK's complete reference.

Verification:

- exhaustive line transforms and deterministic game sequences pass;
- model truth remains correct across interrupted/queued animations;
- host visual fixtures and available device performance checks pass;
- compound merges meet 30 FPS on each certified device profile.

## Milestone 9: Release, Documentation, and Completion Audit

Files:

- add `docs/architecture/`, `docs/release/`, `docs/troubleshooting/`
- add `docs/ROADMAP.md`, `SECURITY.md`, root `README.md` updates
- add release and certification scripts

Work:

1. Complete architecture, package-format, security limitations, key ceremony,
   signing, incident response, SDK, profile porting, build/install/remove,
   troubleshooting, and error-code documentation.
2. Record post-v1 online entitlement, revocation, remote attestation,
   device-bound licensing, and secure-boot/TEE work in the roadmap.
3. Generate reproducible release artifacts, SBOM, licenses, inspection reports,
   checksums, AMRs, platform payloads, signed official app packages, and profile
   evidence.
4. Run tests, production builds, `git diff --check`, secret scans, ELF checks,
   AMR/package inspection, and requirement-by-requirement delivery audit.
5. State hardware limitations accurately when a required dictionary pen is not
   physically available; never promote simulator evidence to certification.

Completion requires evidence for both Falcon AMRs, the non-killing hole path,
the signed installer and rejection paths, the desktop and SDK, development
documentation, the animated 2048 app, production packaging, and all applicable
verification gates.

