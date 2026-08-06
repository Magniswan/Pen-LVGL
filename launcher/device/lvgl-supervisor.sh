#!/bin/sh
set -eu

PROFILE_ID=OVERHEAD_Y01_SKU_CHN_PRO
APP_ROOT=/userdisk/apps/lvgl-poc/current
RUN_ROOT=/run/lvgl-poc
LOG_ROOT=/userdata/applog/lvgl-poc
PROC_ROOT=/proc
TEST_MODE=${LVGL_SUPERVISOR_TEST_MODE:-0}

if [ "$TEST_MODE" = "1" ]; then
    APP_ROOT=${LVGL_TEST_APP_ROOT:?LVGL_TEST_APP_ROOT is required in test mode}
    RUN_ROOT=${LVGL_TEST_RUN_ROOT:?LVGL_TEST_RUN_ROOT is required in test mode}
    LOG_ROOT=${LVGL_TEST_LOG_ROOT:?LVGL_TEST_LOG_ROOT is required in test mode}
    PROC_ROOT=${LVGL_TEST_PROC_ROOT:?LVGL_TEST_PROC_ROOT is required in test mode}
fi

SESSION=$APP_ROOT/bin/lvgl_session
LVGL_LAUNCHER=$APP_ROOT/bin/lvgl_launcher
LVGL_POC=$APP_ROOT/bin/lvgl_poc
FOCUS_TIMER=$APP_ROOT/bin/focus_timer
ASSET=$APP_ROOT/assets/poc_badge.png
MANIFEST=$APP_ROOT/manifest.env
LOCK_DIR=$RUN_ROOT/lock
STATE_FILE=$RUN_ROOT/status.env
LOG_FILE=$LOG_ROOT/supervisor.log
GUARDIAN_COMMAND='/usr/bin/guardian_run /usr/bin/runDictPen'
RUN_DICT_COMMAND='/bin/sh /usr/bin/runDictPen'
MINIAPP_EXE=/usr/bin/miniapp

APP_PID=
FALCON_CHANGED=0
LOCK_HELD=0
FINAL_STATE=error
FINAL_CODE=1

rotate_log() {
    mkdir -p "$LOG_ROOT"
    [ -f "$LOG_FILE" ] || return 0
    size=$(wc -c < "$LOG_FILE" 2>/dev/null || printf '0')
    [ "$size" -lt 262144 ] && return 0
    rm -f "$LOG_FILE.3"
    [ ! -f "$LOG_FILE.2" ] || mv "$LOG_FILE.2" "$LOG_FILE.3"
    [ ! -f "$LOG_FILE.1" ] || mv "$LOG_FILE.1" "$LOG_FILE.2"
    mv "$LOG_FILE" "$LOG_FILE.1"
}

log() {
    printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S')" "$*" >> "$LOG_FILE"
}

write_state() {
    state=$1
    code=${2:-0}
    mkdir -p "$RUN_ROOT"
    {
        printf 'STATE=%s\n' "$state"
        printf 'SUPERVISOR_PID=%s\n' "$$"
        printf 'APP_PID=%s\n' "${APP_PID:-0}"
        printf 'RESULT=%s\n' "$code"
        printf 'PROFILE_ID=%s\n' "$PROFILE_ID"
        printf 'UPDATED_AT=%s\n' "$(date '+%Y-%m-%dT%H:%M:%S')"
    } > "$STATE_FILE.tmp"
    mv "$STATE_FILE.tmp" "$STATE_FILE"
}

cmdline_for() {
    pid=$1
    [ -r "$PROC_ROOT/$pid/cmdline" ] || return 1
    tr '\000' ' ' < "$PROC_ROOT/$pid/cmdline" | sed 's/[[:space:]]*$//'
}

pids_with_cmdline() {
    expected=$1
    for proc in "$PROC_ROOT"/[0-9]*; do
        [ -d "$proc" ] || continue
        pid=${proc##*/}
        actual=$(cmdline_for "$pid" 2>/dev/null || true)
        [ "$actual" = "$expected" ] && printf '%s\n' "$pid"
    done
}

pids_with_exe() {
    expected=$1
    for proc in "$PROC_ROOT"/[0-9]*; do
        [ -d "$proc" ] || continue
        pid=${proc##*/}
        if [ "$TEST_MODE" = "1" ] && [ -f "$proc/exe.path" ]; then
            actual=$(sed -n '1p' "$proc/exe.path")
        else
            actual=$(readlink "$proc/exe" 2>/dev/null || true)
        fi
        [ "$actual" = "$expected" ] && printf '%s\n' "$pid"
    done
}

first_pid() {
    "$@" | head -n 1 || true
}

probe_processes() {
    guard=$(first_pid pids_with_cmdline "$GUARDIAN_COMMAND")
    run_dict=$(first_pid pids_with_cmdline "$RUN_DICT_COMMAND")
    miniapp=$(first_pid pids_with_exe "$MINIAPP_EXE")
    printf 'GUARD_PID=%s\nRUN_DICT_PID=%s\nMINIAPP_PID=%s\n' \
        "${guard:-0}" "${run_dict:-0}" "${miniapp:-0}"
}

stop_pid() {
    pid=$1
    [ -n "$pid" ] || return 0
    if [ "$TEST_MODE" = "1" ]; then
        log "test stop pid=$pid"
        return 0
    fi
    kill -TERM "$pid" 2>/dev/null || true
    attempts=0
    while [ "$attempts" -lt 20 ] && kill -0 "$pid" 2>/dev/null; do
        sleep 0.1
        attempts=$((attempts + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
    fi
}

validate_release() {
    [ -f "$MANIFEST" ] || { log "error=manifest_missing path=$MANIFEST"; return 1; }
    [ -f "$ASSET" ] || { log "error=asset_missing path=$ASSET"; return 1; }

    manifest_profile=$(sed -n 's/^PROFILE_ID=//p' "$MANIFEST" | head -n 1 | tr -d '\r')
    [ "$manifest_profile" = "$PROFILE_ID" ] || {
        log "error=profile_mismatch expected=$PROFILE_ID actual=${manifest_profile:-missing}"
        return 1
    }
    validate_binary SESSION_SHA256 "$SESSION" || return 1
    validate_binary LAUNCHER_SHA256 "$LVGL_LAUNCHER" || return 1
    validate_binary POC_SHA256 "$LVGL_POC" || return 1
    validate_binary FOCUS_TIMER_SHA256 "$FOCUS_TIMER" || return 1
}

validate_binary() {
    key=$1
    path=$2
    [ -x "$path" ] || { log "error=binary_not_executable key=$key path=$path"; return 1; }
    expected_hash=$(sed -n "s/^$key=//p" "$MANIFEST" | head -n 1 | tr -d '\r')
    case "$expected_hash" in
        ''|*[!0-9a-fA-F]*) log "error=manifest_hash_invalid key=$key"; return 1;;
    esac
    [ "${#expected_hash}" -eq 64 ] || {
        log "error=manifest_hash_length key=$key"
        return 1
    }
    [ "$TEST_MODE" = "1" ] && return 0
    actual_hash=$(sha256sum "$path" | awk '{print $1}')
    [ "$actual_hash" = "$expected_hash" ] || {
        log "error=binary_hash_mismatch key=$key expected=$expected_hash actual=$actual_hash"
        return 1
    }
}

acquire_lock() {
    mkdir -p "$RUN_ROOT"
    if ! mkdir "$LOCK_DIR" 2>/dev/null; then
        log "result=already_running"
        return 1
    fi
    LOCK_HELD=1
    printf '%s\n' "$$" > "$LOCK_DIR/pid"
}

stop_falcon() {
    guard=$(first_pid pids_with_cmdline "$GUARDIAN_COMMAND")
    run_dict=$(first_pid pids_with_cmdline "$RUN_DICT_COMMAND")
    miniapp=$(first_pid pids_with_exe "$MINIAPP_EXE")
    log "falcon found guard=${guard:-none} runDict=${run_dict:-none} miniapp=${miniapp:-none}"
    [ -n "$guard" ] || { log "error=falcon_guard_not_found"; return 1; }

    FALCON_CHANGED=1
    stop_pid "$guard"
    for pid in $(pids_with_cmdline "$RUN_DICT_COMMAND"); do stop_pid "$pid"; done
    for pid in $(pids_with_exe "$MINIAPP_EXE"); do stop_pid "$pid"; done
    [ "$TEST_MODE" = "1" ] || sleep 1
    log "falcon stopped"
}

falcon_is_ready() {
    [ -n "$(first_pid pids_with_cmdline "$GUARDIAN_COMMAND")" ] &&
        [ -n "$(first_pid pids_with_exe "$MINIAPP_EXE")" ]
}

restore_falcon_once() {
    if [ "$TEST_MODE" = "1" ]; then
        log "test falcon restored"
        return 0
    fi
    if [ -z "$(first_pid pids_with_cmdline "$GUARDIAN_COMMAND")" ]; then
        rm -f "$RUN_ROOT/falcon-guardian.pid"
        /sbin/start-stop-daemon -S -b -m -p "$RUN_ROOT/falcon-guardian.pid" \
            -x /usr/bin/guardian_run -- /usr/bin/runDictPen >> "$LOG_FILE" 2>&1 || true
        log "falcon guardian start requested"
    fi
    attempts=0
    while [ "$attempts" -lt 30 ]; do
        falcon_is_ready && return 0
        sleep 1
        attempts=$((attempts + 1))
    done
    return 1
}

restore_falcon() {
    [ "$FALCON_CHANGED" -eq 1 ] || return 0
    write_state restoring "$FINAL_CODE"
    for delay in 1 2 5; do
        if restore_falcon_once; then
            FALCON_CHANGED=0
            log "falcon restored"
            return 0
        fi
        log "warn=falcon_restore_retry delay_s=$delay"
        sleep "$delay"
    done
    log "error=falcon_restore_failed"
    return 1
}

cleanup() {
    trap - EXIT INT TERM HUP
    if [ -n "${APP_PID:-}" ] && [ "$TEST_MODE" != "1" ] && kill -0 "$APP_PID" 2>/dev/null; then
        kill -TERM "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi
    APP_PID=
    if ! restore_falcon; then
        FINAL_STATE=error
        FINAL_CODE=70
    fi
    write_state "$FINAL_STATE" "$FINAL_CODE"
    if [ "$LOCK_HELD" -eq 1 ]; then
        rm -rf "$LOCK_DIR"
        LOCK_HELD=0
    fi
    log "supervisor exit state=$FINAL_STATE code=$FINAL_CODE"
}

on_signal() {
    FINAL_STATE=error
    FINAL_CODE=143
    exit 143
}

command=${1:-run}
if [ "$command" = "probe" ]; then
    probe_processes
    exit 0
fi
if [ "$command" != "run" ]; then
    echo "usage: $0 [run|probe]" >&2
    exit 64
fi

rotate_log
mkdir -p "$RUN_ROOT" "$LOG_ROOT"
log "supervisor start pid=$$ profile=$PROFILE_ID test=$TEST_MODE"

validate_release || { write_state error 65; exit 65; }
acquire_lock || exit 75
trap cleanup EXIT
trap on_signal INT TERM HUP
write_state launching 0
[ "$TEST_MODE" = "1" ] || sleep 1
stop_falcon || { FINAL_CODE=69; exit "$FINAL_CODE"; }

write_state running 0
if [ "$TEST_MODE" = "1" ]; then
    "$SESSION" --bin-dir "$APP_ROOT/bin" >> "$LOG_FILE" 2>&1 &
else
    cp "$ASSET" /tmp/lvgl-poc-logo.png
    "$SESSION" --bin-dir "$APP_ROOT/bin" >> "$LOG_FILE" 2>&1 &
fi
APP_PID=$!
write_state running 0
log "lvgl session started pid=$APP_PID"

set +e
wait "$APP_PID"
FINAL_CODE=$?
set -e
APP_PID=
if [ "$FINAL_CODE" -eq 0 ]; then
    FINAL_STATE=idle
else
    FINAL_STATE=error
fi
log "lvgl session exit code=$FINAL_CODE"
exit "$FINAL_CODE"
