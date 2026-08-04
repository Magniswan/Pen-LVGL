#!/bin/sh
set -eu

APP=${1:-}
LIMIT_SECONDS=${2:-300}
DRY_RUN=0
if [ "${3:-}" = "--dry-run" ]; then DRY_RUN=1; fi

LOG_FILE=/tmp/lvgl-poc-wrapper.log
APP_PID=
GUARD_PID=
RUN_DICT_PID=
FALCON_STOPPED=0

log() {
    printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S')" "$*" >> "$LOG_FILE"
}

cmdline_for() {
    tr '\000' ' ' < "/proc/$1/cmdline" 2>/dev/null || true
}

pids_containing() {
    needle=$1
    for proc in /proc/[0-9]*; do
        pid=${proc##*/}
        [ -r "$proc/cmdline" ] || continue
        line=$(cmdline_for "$pid")
        case "$line" in
            *"$needle"*) printf '%s\n' "$pid";;
        esac
    done
}

pid_with_exe() {
    expected=$1
    for proc in /proc/[0-9]*; do
        pid=${proc##*/}
        exe=$(readlink "/proc/$pid/exe" 2>/dev/null || true)
        [ "$exe" = "$expected" ] && printf '%s\n' "$pid"
    done
}

first_pid() {
    value=$("$@" | head -n 1 || true)
    [ -n "$value" ] && printf '%s\n' "$value"
}

find_processes() {
    GUARD_PID=$(first_pid pids_containing '/usr/bin/guardian_run /usr/bin/runDictPen' || true)
    RUN_DICT_PID=$(first_pid pids_containing '/usr/bin/runDictPen' || true)
    log "found guard=${GUARD_PID:-none} runDict=${RUN_DICT_PID:-none} miniapp=$(pid_with_exe /usr/bin/miniapp | tr '\n' ' ')"
}

stop_pid() {
    pid=$1
    [ -n "$pid" ] || return 0
    kill -TERM "$pid" 2>/dev/null || true
    tries=0
    while [ "$tries" -lt 20 ] && kill -0 "$pid" 2>/dev/null; do
        sleep 0.1
        tries=$((tries + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
    fi
}

stop_falcon() {
    find_processes
    stop_pid "${GUARD_PID:-}"
    for pid in $(pids_containing '/usr/bin/runDictPen'); do stop_pid "$pid"; done
    for pid in $(pid_with_exe /usr/bin/miniapp); do stop_pid "$pid"; done
    FALCON_STOPPED=1
    sleep 1
    log "falcon stopped"
}

restore_falcon() {
    [ "$FALCON_STOPPED" -eq 1 ] || return 0
    FALCON_STOPPED=0
    if [ -z "$(first_pid pids_containing '/usr/bin/guardian_run /usr/bin/runDictPen' || true)" ]; then
        rm -f /tmp/lvgl-poc-guardian.pid
        /sbin/start-stop-daemon -S -b -m -p /tmp/lvgl-poc-guardian.pid -x /usr/bin/guardian_run -- /usr/bin/runDictPen \
            >> /tmp/lvgl-poc-restore.log 2>&1 || true
        log "falcon guardian restarted"
    fi
    tries=0
    while [ "$tries" -lt 30 ]; do
        if [ -n "$(pid_with_exe /usr/bin/miniapp | head -n 1)" ]; then
            log "falcon miniapp restored"
            return 0
        fi
        sleep 1
        tries=$((tries + 1))
    done
    log "falcon restore timeout"
    return 1
}

cleanup() {
    trap - EXIT INT TERM HUP
    if [ -n "${APP_PID:-}" ] && kill -0 "$APP_PID" 2>/dev/null; then
        kill -TERM "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi
    restore_falcon || true
}

if [ -z "$APP" ]; then
    echo "usage: $0 <app> [timeout_seconds] [--dry-run]" >&2
    exit 64
fi
if [ ! -x "$APP" ]; then
    echo "app is not executable: $APP" >&2
    exit 65
fi

: > "$LOG_FILE"
trap cleanup EXIT INT TERM HUP
log "wrapper start app=$APP limit=$LIMIT_SECONDS dry_run=$DRY_RUN"
find_processes
if [ "$DRY_RUN" -eq 1 ]; then
    log "dry-run complete"
    cat "$LOG_FILE"
    exit 0
fi

stop_falcon
"$APP" >> "$LOG_FILE" 2>&1 &
APP_PID=$!
log "app pid=$APP_PID"

elapsed=0
while kill -0 "$APP_PID" 2>/dev/null; do
    if [ "$elapsed" -ge "$LIMIT_SECONDS" ]; then
        log "app timeout pid=$APP_PID"
        kill -TERM "$APP_PID" 2>/dev/null || true
        sleep 2
        kill -KILL "$APP_PID" 2>/dev/null || true
        break
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done

wait "$APP_PID" 2>/dev/null || APP_RC=$?
APP_PID=
log "app exit=${APP_RC:-0}"
exit "${APP_RC:-0}"
