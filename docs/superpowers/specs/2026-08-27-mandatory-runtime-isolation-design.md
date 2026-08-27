# Mandatory Runtime Isolation Design

**Status:** Approved architecture; implementation has not started.

**Date:** 2026-08-27

## Objective

Make cgroup v2 resource control and a minimal read-only mount namespace mandatory
for every foreground LVGL application started by `lvgl-sessiond`. A production
profile that cannot prove all required kernel capabilities is unsupported and
must fail closed before any application code executes.

This milestone closes the two host-side isolation gaps recorded in the threat
model and roadmap. It does not claim target-hardware certification, secure boot,
or resistance to a malicious kernel.

## Context and Evidence

The current runtime already provides:

- exact-child supervision without killing Falcon or `miniapp`;
- an executable opened from a verified release and launched with `fexecve`;
- explicit inherited descriptors and closure of unrelated descriptors;
- dedicated non-root application UID/GID;
- `RLIMIT_AS`, `RLIMIT_CPU`, `RLIMIT_CORE`, `RLIMIT_FSIZE`, `RLIMIT_NOFILE`,
  `RLIMIT_NPROC`, `RLIMIT_MEMLOCK`, and `RLIMIT_STACK`;
- `PR_SET_PDEATHSIG`, non-dumpable application processes, and mandatory AArch64
  seccomp before runtime readiness;
- brokered storage, installer, touch, display, and lifecycle services.

The evaluated target family uses Buildroot Linux 5.10.160. Its cross-toolchain
does not expose `mount_setattr`, `open_tree`, or `move_mount`; the design therefore
uses Linux 5.10-compatible legacy mount namespace operations. Current AArch64
release binaries require the dynamic loader and libraries under `/lib` and
`/usr/lib`. Applications do not need a general host filesystem view.

## Security Goals

1. A signed but exploited application cannot exceed its certified memory, CPU,
   or process-count limits.
2. Application code begins only after the parent has attached it to a fully
   configured, platform-owned cgroup.
3. An application sees only its verified release, runtime libraries, and the
   descriptors explicitly granted by the platform.
4. The application filesystem is read-only. Persistent writes remain possible
   only through the storage broker.
5. Every capability or setup failure is observable and prevents application
   execution; there is no degraded production mode or install-anyway bypass.
6. Isolation lifecycle operations affect only the exact LVGL child and its
   platform-owned cgroup. Falcon and `miniapp` remain running and are never
   signalled.

## Non-Goals and Trust Boundary

- This design does not protect against an attacker who controls the kernel,
  boot chain, or the already-running root `lvgl-sessiond` process.
- Root resistance assumes an intact, officially signed platform release and an
  honest kernel. Secure/verified boot and hardware-backed attestation are
  separate deployment requirements.
- This milestone does not certify any physical device or firmware profile.
- Networking, device passthrough beyond existing brokered descriptors, and
  online services are outside this milestone.
- The design does not make third-party signing keys trusted. Production remains
  pinned to the official Ed25519 release key.

## Approaches Considered

### Selected: session daemon constructs isolation before privilege drop

`lvgl-sessiond` already verifies releases, owns the privileged device
descriptors, forks the exact application child, and supervises its lifetime. It
is the only component with enough context to bind the verified release and
apply policy without accepting untrusted paths. Keeping isolation here also
avoids another privileged protocol.

### Rejected: separate privileged sandbox helper

A helper would introduce another root process, IPC parser, authorization model,
and lifecycle race while providing no necessary capability that the session
daemon lacks.

### Rejected: application self-sandbox after `exec`

The application would execute before isolation was complete, would not retain
the required mount privileges after UID/GID drop, and could choose not to apply
the policy.

## Architecture

The implementation adds two internal session-daemon components behind narrow,
testable interfaces:

- `CgroupV2Manager` preflights the cgroup filesystem, creates one cgroup per
  launch, writes exact limits, attaches the blocked child, observes population,
  and removes the empty cgroup.
- `MountNamespaceBuilder` creates a private mount namespace and a minimal root
  from already validated directory descriptors, verifies read-only mounts, and
  enters it before privilege drop.

Production code uses Linux syscalls and descriptor-relative filesystem
operations. Host tests use injected backends that record calls and return
controlled failures. Neither component accepts application-controlled cgroup
names, mount destinations, source paths, or policy text.

Every first-party application, including Desktop, Installer, and 2048, follows
the same path. The session daemon and Falcon AMRs remain outside application
cgroups.

## Certified Profile Contract

The signed `profile.env` schema expands from exactly 16 fields to exactly 19
fields by adding:

```text
CGROUP_V2_CERTIFIED=1
CGROUP_SWAP_LIMIT_CERTIFIED=1
MOUNT_NAMESPACE_CERTIFIED=1
```

Field order is not significant. Unknown, duplicate, missing, or non-`1`
certification fields are rejected. The canonical device-profile JSON schema
adds matching boolean fields under `certification`:

```json
{
  "cgroupV2Certified": true,
  "cgroupSwapLimitCertified": true,
  "mountNamespaceCertified": true
}
```

Release staging may emit the environment fields only when the source profile
contains all three true values and its target evidence bundle passes. Existing
profiles remain false or absent until recertified and therefore cannot produce a
production platform release with this feature marked available.

## Cgroup v2 Policy

### Preflight

Before accepting a launch, the manager:

1. opens `/sys/fs/cgroup` as a directory without following a symbolic link;
2. verifies `statfs` reports `CGROUP2_SUPER_MAGIC`;
3. verifies `cpu`, `memory`, and `pids` appear as exact tokens in
   `cgroup.controllers`;
4. verifies the administrator-provisioned parent has delegated those three
   controllers, then creates or opens the root-owned platform subtree
   `/sys/fs/cgroup/lvgl-platform` using descriptor-relative, no-follow
   operations;
5. confirms no process, including `lvgl-sessiond`, is a member of the platform
   subtree, writes exactly `+cpu +memory +pids` to its
   `cgroup.subtree_control`, and reads back all three enabled tokens;
6. verifies `memory.swap.max` is present and writable in a disposable child
   cgroup during profile certification.

The service account must receive writable delegation only for the
platform-owned subtree. A legacy cgroup hierarchy, hybrid setup, missing
controller, symlink, unexpected file type, or insufficient delegation makes the
profile unusable.

### Per-launch cgroup

Each launch uses a root-controlled name composed from an internal monotonic
launch nonce and child PID. Application identifiers and package text never form
filesystem names. The manager creates the directory with mode `0700` and rejects
a pre-existing name instead of reusing it.

Before the child is released, the parent writes and reads back:

| Control | Required value | Reason |
| --- | --- | --- |
| `memory.max` | signed `limits.memoryMiB * 1,048,576` bytes | hard addressable-memory budget |
| `memory.high` | `floor(memory.max * 9 / 10)` bytes | reclaim pressure before hard failure |
| `memory.swap.max` | `0` | prevent disk-backed escape from the memory budget |
| `pids.max` | `1` | application cannot create descendants |
| `cpu.max` | `100000 100000` | at most one full CPU in aggregate |

`memory.max` must fit in unsigned 64-bit arithmetic and be at least 8 MiB.
`memory.high` is computed without multiplication overflow as
`(memory.max / 10) * 9 + ((memory.max % 10) * 9) / 10`. Existing signed
`RLIMIT_CPU` remains the lifetime CPU-time ceiling; `cpu.max` limits short-term
parallel consumption. Existing rlimits are defense in depth and are not relaxed.

The manager treats short writes, whitespace-normalized read-back mismatches,
unexpected `max`, truncation, or any controller error as a launch failure.

### Parent-child activation barrier

The daemon creates a close-on-exec activation pipe before `fork`.

- The child closes the write end and blocks on a single-byte activation token as
  its first operation.
- The parent creates the cgroup, applies and verifies every control, writes the
  child PID to `cgroup.procs`, and verifies membership.
- Only then does the parent write the activation token.
- EOF, an invalid token, or a parent-side failure makes the child exit with the
  reserved pre-exec isolation failure status. It never opens application code.

This barrier prevents a scheduling race between `fork` and cgroup attachment.

## Minimal Read-only Mount Namespace

After activation and while still root, the child performs:

1. `unshare(CLONE_NEWNS)`;
2. remounts `/` propagation private with `MS_REC | MS_PRIVATE`;
3. creates
   `/run/lvgl-platform/namespaces/<launch-nonce>-<child-pid>` beneath a
   pre-opened root-owned, mode-`0700` directory, then mounts a tmpfs with exact
   options `size=1m,nr_inodes=64,mode=0755,nodev,nosuid,noexec` as the staging
   root;
4. creates fixed destination directories inside that root;
5. bind-mounts the verified release directory as `/app`;
6. bind-mounts `/lib` and `/usr/lib` for the dynamic loader and audited runtime
   dependencies;
7. remounts every bind with
   `MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV`; the three bind
   mounts do not use `MS_NOEXEC` because the application and dynamic loader must
   map executable code from them, while the otherwise empty staging tmpfs keeps
   `MS_NOEXEC`;
8. verifies mount flags from `/proc/self/mountinfo` while still outside the new
   root;
9. `chdir` to the staging root, `chroot`, and changes working directory to
   `/app`;
10. closes every descriptor and directory reference to the old root except the
    already-open regular executable and explicit broker/device descriptors,
    then verifies the current root and working directory before privilege drop.

Mount sources are not reconstructed from caller paths. The parent passes
directory descriptors for the already verified active release, `/lib`, and
`/usr/lib`. Before `chroot`, the child refers to each through its own
`/proc/self/fd/<validated-dirfd>` solely as a bind source. `/proc` is not mounted
in the final root.

### Final filesystem view

| Path | Source | Flags | Purpose |
| --- | --- | --- | --- |
| `/app` | verified active-release directory FD | read-only, nosuid, nodev | executable and signed resources |
| `/lib` | host `/lib` opened by the platform | read-only, nosuid, nodev | AArch64 loader and core libraries |
| `/usr/lib` | host `/usr/lib` opened by the platform | read-only, nosuid, nodev | audited shared libraries |

The final root contains no `/proc`, `/sys`, host `/dev`, `/etc`, `/data`,
`/userdata`, `/userdisk`, `/root`, `/home`, temporary write area, UNIX socket
directory, or package inbox. Empty mountpoint directories are not retained when
they are not one of the three entries above.

DRM, touch, storage, installer, lifecycle control, and bounded log output remain
available only through explicit inherited descriptors. All application state
writes go through the storage broker. Dynamic dependency closure is an input to
profile certification: a binary requiring a library outside the two certified
library roots is rejected from that release rather than exposing another host
tree.

## Exact Launch Sequence

1. Verify the official package signature, installed release manifest, rollback
   state, application policy, and certified device profile.
2. Open the executable, active release directory, `/lib`, and `/usr/lib` using
   descriptor-relative, no-follow validation; record device/inode identity.
3. Create the namespace staging directory and close-on-exec activation pipe.
4. `fork` the exact application child.
5. Child blocks on the activation pipe without executing package code.
6. Parent creates and configures the unique cgroup, attaches the child, and
   verifies membership.
7. Parent releases the child with the activation token.
8. Child constructs, verifies, and enters the minimal read-only root.
9. Child closes nonessential descriptors and applies the existing rlimits,
   supplementary-group removal, UID/GID drop, non-dumpable state, and parent
   death signal.
10. Child invokes `execveat(executable_fd, "", argv, envp, AT_EMPTY_PATH)` on the
    already-open native ELF executable. Direct `execveat` avoids a libc
    `fexecve` fallback through `/proc`, which is intentionally absent.
11. The runtime installs mandatory AArch64 seccomp before emitting READY.
12. Parent supervises the exact PID and, after exit, reconciles the empty cgroup
    and staging directory before another launch.

READY is invalid unless all preceding stages succeeded. The desktop must remain
or be restored when a launch fails.

## Failure, Cleanup, and Recovery

Stable internal failure stages use these prefixes:

```text
isolation.cgroup.preflight
isolation.cgroup.create
isolation.cgroup.limits
isolation.cgroup.attach
isolation.cgroup.cleanup
isolation.mount.unshare
isolation.mount.root
isolation.mount.bind
isolation.mount.readonly
isolation.mount.chroot
```

The UI receives a bounded, localized category and correlation ID, not raw host
paths, kernel strings, package contents, or secrets. A root-owned, size-bounded
lifecycle audit records the correlation ID, profile ID, application ID, release
digest, exact stage, normalized errno category, and cleanup result.

After the child exits, the daemon reads `cgroup.events` and requires
`populated 0` before removing only that launch's cgroup. It never uses broad
process-name matching, `kill(-1, ...)`, cgroup-wide kill against an unverified
path, or any signal to Falcon/`miniapp`. Because `pids.max=1` and seccomp denies
process creation, a populated cgroup after exact-child exit is a security
invariant failure.

An unresolved cgroup or namespace staging directory is quarantined in daemon
state, recorded in the audit, and blocks subsequent application launches until
startup reconciliation proves it empty and safely removes it. Reconciliation
uses only root-owned names matching persisted launch records and descriptor
identity; it never recursively deletes an unverified path. A power loss may
leave a directory entry but cannot leave a running process; the next boot must
perform the same empty/population checks before removal.

## Test and Evidence Plan

### Deterministic host tests

An injected fake syscall/filesystem backend proves the exact launch ordering and
that application execution is impossible before successful attachment. Negative
cases cover:

- wrong cgroup filesystem magic, hybrid hierarchy, and missing controller;
- symlink or unexpected type in the platform subtree;
- duplicate or stale per-launch cgroup;
- malformed controller files and every limit write/read-back failure;
- attach or membership-verification failure;
- activation pipe EOF, wrong token, and parent death;
- `unshare`, propagation, tmpfs, bind, remount, mount verification, `chroot`, and
  old-root reachability failures;
- cleanup while populated and persisted-state mismatch;
- overflow, under-minimum memory, and unsigned/signed conversion boundaries.

Source-level regression tests assert there is no degraded-isolation flag, no
application-derived path or cgroup name, no broad kill path, and no signal sent
to Falcon or `miniapp`.

### Build and release tests

- Native host unit and source tests pass.
- AArch64 production cross-build compiles the syscall implementation against the
  Linux 5.10-compatible toolchain.
- Profile parser tests accept exactly the new 19-field certified form and reject
  missing, false, duplicate, unknown, malformed, or legacy 16-field production
  inputs.
- Release staging tests prove uncertified JSON profiles cannot emit certified
  environment fields.
- Dependency inspection proves every shipped application resolves from `/lib`,
  `/usr/lib`, or its verified `/app` release without an `RPATH` escaping those
  roots.

### Deferred target-hardware certification

No device or ADB operation belongs to this implementation milestone. A profile
may be switched to the three certified values only after later target evidence
demonstrates:

- hard memory enforcement and expected cgroup OOM behavior;
- one-CPU quota behavior under sustained load;
- failed `fork`, `clone`, and process-fan-out attempts;
- inability to read or remount the old root, `/proc`, `/sys`, devices, user data,
  and package inbox;
- successful resource reads from `/app` and state operations through the broker;
- cleanup after normal exit, crash, daemon restart, and power interruption;
- uninterrupted Falcon/`miniapp`, touch forwarding, display overlay, and desktop
  recovery.

Until that evidence exists, current profiles stay uncertified and production
launch remains closed.

## Acceptance Criteria

The implementation milestone is accepted when:

1. every platform-launched application uses the activation barrier, unique
   cgroup, and minimal read-only root;
2. all configured cgroup values are read back before application activation;
3. any setup or verification failure prevents `execveat` and runtime READY;
4. the final root exposes exactly `/app`, `/lib`, and `/usr/lib`, with no writable
   filesystem;
5. storage and installer operations continue only through existing authenticated
   brokers;
6. the strict certified profile and release pipeline reject legacy or partially
   certified inputs;
7. host tests, profile/release tests, AArch64 production build, dependency audit,
   and private-key scan pass;
8. documentation clearly distinguishes host implementation evidence from later
   hardware certification and secure-boot guarantees;
9. no new code can stop, kill, replace, or inject into Falcon/`miniapp`.

## Rollback and Compatibility

The change is intentionally incompatible with legacy production profiles. A
code rollback must also roll back to a separately signed platform release and
its matching profile schema; a new daemon must never reinterpret a 16-field
profile as isolated. Installed application packages remain format-compatible
because limits already come from signed manifests and no new application
permission is introduced.

If a certified firmware later loses a required controller, swap limit, mount
operation, or library-layout invariant, the platform reports an unsupported
profile and keeps the desktop/Falcon environment available. It does not weaken
isolation to preserve application launch.
