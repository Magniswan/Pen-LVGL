# LVGL Dictionary-Pen Commercial Platform Design

Date: 2026-08-24

Status: approved by standing user authorization

## 1. Product Goal

Build a distributable LVGL application platform for Youdao Falcon dictionary
pens. The platform provides two Falcon AMR applications, a persistent native
LVGL runtime, a signed application-package ecosystem, a desktop, an on-device
application installer, an application SDK, and a polished 2048 reference game.

The platform is multi-profile by design. Hardware support is claimed only for
an exact model and firmware combination that passed the certification matrix.
The existing `OVERHEAD_Y01_SKU_CHN_PRO` firmware 4.8.6 profile is the first
recorded evidence, not a hard-coded platform assumption.

## 2. Decisions

- Each LVGL application runs in its own native process.
- Only packages signed by the official release root are installable.
- Version 1 works offline. Online licensing, revocation, and remote attestation
  are a post-v1 roadmap item, with protocol fields reserved now.
- The Falcon launcher remains alive and uses a full-screen `<hole>` plus a
  transparent Falcon touch layer. It never stops Falcon, `miniapp`,
  `runDictPen`, or the guardian.
- Falcon owns touch collection and lifecycle. Native LVGL owns rendering on a
  compatible KMS plane. Touch crosses a bounded local IPC protocol.
- Applications cannot load a `.so` into the desktop or supervisor process.
- Unsupported or ambiguous hardware fails closed and returns to Falcon.
- Official private signing keys never enter this repository, an AMR, a device,
  CI logs, or a developer build.

## 3. Threat Model

The attacker may possess the device, obtain root, read and modify persistent
storage, attach a debugger, replace executables, replay old releases, inspect
process memory, and interrupt power during installation.

Without a manufacturer-controlled secure-boot chain, immutable hardware key,
or trusted execution environment, software on a root-owned device cannot make
an absolute anti-cracking guarantee. Version 1 therefore provides defense in
depth and measurable tamper resistance, while making no false claim that a
root attacker can never patch the verifier or copy runtime plaintext.

Version 1 defends against:

- unsigned, corrupted, truncated, repacked, or path-traversal packages;
- replacement of installed application files;
- downgrade and replay through the official installer;
- package-ID takeover and cross-application data access through normal APIs;
- shell injection, arbitrary launcher arguments, symlink races, and partial
  installation state;
- accidental key disclosure from the development or device environment;
- application crashes, hangs, fork bombs, oversized packages, and resource
  exhaustion within the capabilities of the target kernel.

Root-resistance measures include independent verification in the Falcon
installer bridge, native trust service, and session launcher; compiled-in key
fingerprint agreement; launch-time remeasurement; redundant rollback state;
immutable version directories; least-privilege application processes; bounded
IPC; and fail-closed recovery. These measures raise the cost of a coherent
patch but do not redefine the hardware trust boundary.

## 4. System Architecture

```text
Falcon desktop
├── LVGL Platform Manager.amr
│   └── install / repair / upgrade / remove the platform
└── LVGL Launcher.amr (Falcon process remains alive)
    ├── full-screen <hole>
    ├── transparent touch and lifecycle layer
    └── native JSAPI bridge
         │ fixed local control protocol + touch frames
         ▼
Native platform
├── lvgl-sessiond       display/session owner and process supervisor
├── lvgl-trust          package and installed-tree verifier
├── lvgl-desktop        application shell and launcher
├── lvgl-installer      on-device signed-package installer UI
└── application process
    ├── official 2048
    └── future official applications
```

The Falcon launcher starts exactly one `lvgl-sessiond` instance. The session
daemon selects a certified profile, verifies the active platform tree, opens
the configured KMS plane, starts the desktop, and forwards touch frames to the
foreground application. It restores the desktop after an application exits.
Closing the Falcon page stops the session and native children without killing
unrelated Falcon processes.

### 4.1 Repository Layout

```text
platform/
  include/                 public C++ SDK and stable protocol headers
  src/
    crypto/ package/       verification and package parser
    ipc/ profile/          bounded protocols and profile selection
    runtime/ sandbox/      sessions, supervision, limits, isolation
    storage/ logging/      safe state and bounded diagnostics
falcon/
  manager/                 install/uninstall/repair AMR
  launcher/                persistent hole/touch launcher AMR
apps/
  desktop/
  installer/
  game-2048/
profiles/                  signed/certified device profiles and evidence
sdk/
  cmake/ template/ docs/   application-development kit
tools/
  pack/ sign/ inspect/     host-only package tooling
tests/
  host/ integration/ fuzz/ contract/
docs/
  architecture/ security/ development/ release/ device-porting/
```

Existing PoC components are moved behind these boundaries rather than copied
into a second incompatible runtime.

## 5. Falcon Applications

### 5.1 LVGL Platform Manager AMR

The manager has four explicit operations: Install, Repair, Upgrade, and Remove.
Its native JSAPI accepts no caller-provided command, executable path, or shell
text. It operates only on its embedded, officially signed platform payload and
fixed platform directories.

Installation flow:

1. Read the device identity and select an exact compatible profile.
2. Verify the embedded release envelope and official signature before writing.
3. Check ABI, libc, kernel capabilities, screen geometry, Falcon logical
   geometry, KMS plane capabilities, available space, and existing ownership.
4. Copy into a new versioned staging directory with restrictive permissions.
5. Verify every staged byte, directory type, owner, and mode.
6. Atomically activate the version, preserving one verified rollback release.
7. Run a non-destructive self-test and report a stable result code.

Removal stops the platform session through the fixed protocol, removes only
owned version/state/log directories, and then removes the launcher package.
It never recursively deletes an unresolved, symlinked, empty, root, home, or
shared path. Interrupted removal remains recoverable by running Repair or
Remove again.

### 5.2 LVGL Launcher AMR

The launch page derives its logical geometry from the device adapter before
rendering. When the platform session is ready it shows one `<hole>` covering
the certified native rectangle and mounts a transparent touch layer above it,
following the proven CloudBrowser pattern.

Touch events are normalized into pointer frames containing protocol version,
session nonce, contact ID, phase, logical coordinates, monotonic sequence, and
monotonic timestamp. Move frames are coalesced; start/end/cancel frames are
never dropped. Stale sessions and non-monotonic sequences are rejected.

`onHide`, `onUnload`, native exit, IPC failure, or a profile mismatch cancels
the current generation, removes subscriptions and timers, stops the native
session idempotently, hides the hole, and presents a recoverable Falcon error.

## 6. Device Profiles

Profiles contain identity match rules, ABI/libc constraints, Falcon logical
geometry, physical display geometry, coordinate transforms, touch limits, KMS
driver/connector/CRTC/plane requirements, pixel format, rotation, offsets,
feature flags, and verification evidence.

Profile selection requires one unambiguous exact match. Wildcards may be used
only for fields proven stable across every certified device in that profile.
No application source may branch on model names or contain plane IDs, device
nodes, rotations, or screen constants.

Certification covers install, cold start, two repeated enter/exit cycles,
touch corners and center, overlay ordering, foreground/background, crash,
power interruption during update, idle CPU, sustained animation, and Falcon
recovery. Uncertified profiles are buildable test inputs but not production
compatibility claims.

## 7. Application Package: `.lvapp`

`.lvapp` is a deterministic, streaming-verifiable container, not a generic
ZIP or TAR archive. Version 1 contains:

- fixed magic, format version, header length, entry count, and total length;
- canonical manifest bytes;
- a bounded table of regular-file entries;
- file contents in table order;
- one signature envelope containing key ID, algorithm ID, and signature.

The signature is Ed25519 over a domain-separated SHA-512 digest of every
unsigned package byte. The parser rejects unknown critical flags, duplicate or
non-canonical paths, absolute paths, dot segments, separators other than `/`,
links, devices, sockets, sparse files, invalid UTF-8, oversized names, excessive
entry counts, trailing bytes, overlapping regions, and size arithmetic
overflow. All files also carry SHA-256 content hashes for inspection and
installed-tree remeasurement.

The manifest includes:

- immutable reverse-domain application ID and display metadata;
- semantic version, monotonically increasing release counter, and security
  epoch;
- SDK ABI, package format, minimum platform version, and entry executable;
- supported ABI/profile constraints and resource limits;
- requested capabilities from a closed allowlist;
- per-file path, size, mode, SHA-256, and role;
- official key ID and reserved online-policy fields;
- package UUID used for diagnostics, not as an authorization secret.

Only the offline release tool can add the signature envelope. Normal developer
builds emit an explicitly marked unsigned `.lvapp.dev` artifact that the
production manager and installer always reject.

## 8. Key Management and Release

The official Ed25519 root is generated on an offline release workstation or
hardware token. The repository contains only public keys, fingerprints, a key
ceremony guide, and test-only keys whose IDs are permanently denied by
production builds.

Key rotation uses a threshold transition record signed by both the active and
new root. A new key cannot be introduced through an unsigned configuration
file. Emergency distrust ships as a platform release with a higher security
epoch. Release tooling refuses dirty Git state, non-reproducible input,
uncommitted version changes, test keys, or a package whose manifest differs
from inspected content.

Releases produce the package, detached inspection report, SBOM, build commit,
toolchain identity, public-key fingerprint, and SHA-256/SHA-512 checksums.

## 9. Installation and Rollback

The on-device installer accepts packages only from a dedicated inbox. Import
mechanisms may copy into the inbox but cannot request installation directly.
The installer opens the package without following links, verifies it fully,
checks policy and free space, then writes to:

```text
/userdisk/apps/lvgl-platform/apps/<app-id>/releases/<counter>/
```

Activation is an atomic metadata switch after directory sync. The current and
previous verified releases are retained. State is stored separately and never
executed. A failed first launch automatically rolls back once; repeated failure
quarantines the release and returns to the desktop.

Downgrades are rejected when the release counter or security epoch is below
redundant high-water marks. Because root can edit offline state, these marks are
tamper-evident rather than hardware-immutable in v1. Future online policy uses
the reserved manifest fields to make rollback decisions server-backed.

## 10. Runtime Isolation

Before launching, `lvgl-sessiond` re-verifies the active manifest, signature,
file hashes, key fingerprint, ownership, modes, and path containment. It opens
the executable using a no-follow, beneath-root traversal and launches the
opened object rather than resolving an unchecked string twice.

Where the certified profile supports them, applications receive:

- a dedicated unprivileged UID/GID and private data directory;
- cleared environment and a fixed working directory;
- `no_new_privs`, closed inherited descriptors, resource limits, watchdog,
  parent-death signal, and a closed syscall policy;
- read-only application files and writable access only to private state;
- no shell, package manager, raw block devices, input devices, DRM control,
  Falcon internals, or other application state;
- capability-broker IPC for explicitly declared services.

Missing mandatory isolation capability makes that profile unsupported. Optional
hardening is recorded in certification evidence and never silently advertised.

## 11. Stable Application SDK

Applications link the SDK and implement a small lifecycle contract:

```cpp
class Application {
public:
    virtual AppResult create(AppContext &context) = 0;
    virtual void resume() = 0;
    virtual void suspend() = 0;
    virtual void handle(const InputEvent &event) = 0;
    virtual bool request_close() = 0;
    virtual void destroy() noexcept = 0;
};
```

`AppContext` exposes versioned services for display metrics, theme tokens,
navigation, monotonic time, timers, private storage, safe logging, haptics when
certified, and user settings. Applications do not open DRM or Falcon IPC.

The SDK supplies a CMake toolchain interface, template application, desktop
shell components, responsive layout helpers, asset/font conversion, package
builder, developer inspector, host simulator adapter, mocks, tests, and release
checklist. Public ABI changes use explicit versioning and compatibility tests.

## 12. Desktop and Installer UX

The visual language is **precision instrument**: a calm mineral-blue canvas,
high-contrast porcelain surfaces, restrained copper status accents, and compact
labels suited to a wide dictionary-pen display.

Core tokens:

- Ink `#172033`
- Mineral `#294C60`
- Porcelain `#F5F7F4`
- Mist `#DCE6E7`
- Copper `#D47B45`
- Verified `#2F7D68`

Source Han Sans/Noto Sans CJK is used under a recorded redistributable license.
The desktop is a horizontal application rail with one focused card, adjacent
cards partially visible, a compact system strip, and a signature “verified
seal” that opens package identity and permission details. It avoids generic
mobile icon grids on the unusually wide 960x266 logical canvas.

The installer shows four explicit stages: Inspect, Verify, Install, Result.
Security failures name the failed invariant and offer only safe actions. It
never offers an “install anyway” path. Reduced-motion and low-power policies
are respected across the shell.

## 13. 2048 Reference Application

The 2048 game is both a polished product and an SDK conformance example. It has
deterministic game logic separated from LVGL rendering, seeded tests, persisted
best score, undo limited to one move, restart confirmation, win/lose states,
keyboard/touch adapters, and complete lifecycle cleanup.

The wide layout uses a 4x4 board beside a score/history ribbon. Tiles resemble
layered mineral glass rather than the conventional beige web-game palette.
Motion is concentrated in the board:

- 140 ms cubic ease-out tile translation;
- 110 ms merge pulse from 92% to 108% to 100%;
- a restrained copper halo and score fly-up on merges;
- staggered board entrance on first launch only;
- no continuous decorative animation while idle;
- reduced-motion mode replaces movement with a short opacity transition.

Input is locked only for the commit window, queued to a maximum of one move,
and reconciled with the model before the next frame. Animations never mutate
game truth. The acceptance target on certified hardware is at least 30 FPS
during compound merges, no missed final state, bounded idle CPU, stable memory,
and correct suspend/resume.

## 14. Error Semantics and Recovery

All cross-layer results use stable domains and codes: profile, trust, package,
policy, storage, runtime, display, input, and application. User text is mapped
at the UI boundary; logs retain the code, safe context, build ID, and monotonic
timestamp without secrets or raw package content.

Stop is idempotent everywhere. The order is generation invalidation, timer and
subscription cancellation, input cancellation, child stop, resource close,
wait/reap, then state publication. A watchdog kills only the exact owned child
after a bounded graceful period. Falcon remains usable after every failure.

## 15. Verification Strategy

Host tests cover package parsing, signature vectors, corrupted/truncated files,
path attacks, integer boundaries, rollback policy, profile matching, coordinate
transforms, IPC ordering, storage migration, application lifecycle, 2048 move
logic, animation/model reconciliation, and installer state machines.

Fuzz targets cover the package parser, canonical manifest reader, IPC decoder,
and profile parser. Contract tests ensure all production components embed the
same allowed root keys and deny test keys.

Build verification checks ELF class/machine, interpreter, dependencies,
exported symbols, RPATH/RUNPATH absence, reproducibility, AMR manifests,
embedded payload hashes, `.lvapp` inspection reports, licenses, and SBOM.

Each certified device profile runs fresh install, repair, upgrade, downgrade
rejection, interrupted install, remove/reinstall, hole composition, touch
corners, rapid gestures, two enter/exit cycles, background/foreground, desktop
and application crashes, hang recovery, five-minute 2048 animation, idle CPU,
memory stability, and Falcon continuity. Simulator results never substitute for
KMS, touch, native loading, AMR, or recovery evidence.

## 16. Documentation Deliverables

- platform architecture and process/data-flow guide;
- security model, limitations, key ceremony, signing, rotation, and incident
  response guide;
- `.lvapp` binary format and manifest reference;
- SDK quick start, application lifecycle, UI/layout, storage, logging, testing,
  packaging, signing handoff, and release guides;
- device-profile porting and certification guide;
- Falcon manager/launcher build, package, install, recovery, and uninstall guide;
- 2048 walkthrough explaining model/view separation and animation patterns;
- troubleshooting matrix and stable error-code catalog;
- post-v1 roadmap containing online entitlement, revocation, remote attestation,
  device-bound licenses, and secure-boot/TEE integration.

## 17. Delivery Phases

1. Normalize repository boundaries and shared contracts.
2. Implement crypto, `.lvapp`, host tools, negative tests, and fuzz harnesses.
3. Implement profiles, trust policy, transactional installation, and rollback.
4. Implement persistent Falcon manager and hole launcher AMRs.
5. Implement touch IPC, session daemon, display ownership, and isolation.
6. Implement SDK, desktop, and on-device installer.
7. Implement and visually tune 2048.
8. Build production artifacts, run host and available device verification,
   produce documentation/evidence, and audit every stated requirement.

No phase may claim hardware compatibility beyond its captured device evidence.

