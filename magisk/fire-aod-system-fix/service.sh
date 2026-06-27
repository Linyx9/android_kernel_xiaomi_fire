#!/system/bin/sh

MODDIR=${0%/*}
LOG=/data/adb/fire-aod-system-fix.log

log_msg() {
  echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"
}

wait_for_boot() {
  local i
  i=0
  while [ "$(getprop sys.boot_completed)" != "1" ] && [ "$i" -lt 90 ]; do
    sleep 1
    i=$((i + 1))
  done
}

run_cmd() {
  log_msg "+ $*"
  "$@" >> "$LOG" 2>&1
}

enable_overlay() {
  local name
  name="$1"

  run_cmd cmd overlay enable --user 0 "com.android.shell:${name}" ||
    run_cmd cmd overlay enable "com.android.shell:${name}" ||
    log_msg "Failed to enable overlay ${name}"
}

fabricate_framework_overlay() {
  local name
  local res
  local type
  local value

  name="$1"
  res="$2"
  type="$3"
  value="$4"

  run_cmd cmd overlay disable --user 0 "com.android.shell:${name}"
  run_cmd cmd overlay fabricate --target android --name "$name" "$res" "$type" "$value"
  enable_overlay "$name"
}

apply_aod_settings() {
  run_cmd settings put secure doze_enabled 1
  run_cmd settings put secure doze_always_on 1
  run_cmd settings put secure doze_on_charge 1
  run_cmd settings put secure ambient_pulse_enabled 1
  run_cmd settings put global ambient_enabled 1
}

apply_overlays() {
  fabricate_framework_overlay \
    FireAodDozeComponent \
    android:string/config_dozeComponent \
    string \
    com.android.systemui/.doze.DozeService

  fabricate_framework_overlay \
    FireAodAlwaysOnAvailable \
    android:bool/config_dozeAlwaysOnDisplayAvailable \
    bool \
    true

  fabricate_framework_overlay \
    FireAodDisplayBlanksAfterDoze \
    android:bool/config_displayBlanksAfterDoze \
    bool \
    false

  fabricate_framework_overlay \
    FireAodScreenBrightnessDoze \
    android:integer/config_screenBrightnessDoze \
    integer \
    102

  fabricate_framework_overlay \
    FireAodScreenBrightnessDozeFloat \
    android:dimen/config_screenBrightnessDozeFloat \
    float \
    0.4
}

dump_state() {
  log_msg "Resolved framework resources:"
  cmd overlay lookup android android:string/config_dozeComponent >> "$LOG" 2>&1
  cmd overlay lookup android android:bool/config_dozeAlwaysOnDisplayAvailable >> "$LOG" 2>&1
  cmd overlay lookup android android:bool/config_displayBlanksAfterDoze >> "$LOG" 2>&1
  cmd overlay lookup android android:integer/config_screenBrightnessDoze >> "$LOG" 2>&1
  cmd overlay lookup android android:dimen/config_screenBrightnessDozeFloat >> "$LOG" 2>&1
  log_msg "Framework overlay packages:"
  cmd overlay list android | grep -E 'fire|FireAod|moonlight' >> "$LOG" 2>&1
  log_msg "Dream state:"
  dumpsys dreams | grep -E 'getDozeComponent|mCurrentDream|mForceAmbientDisplayEnabled' >> "$LOG" 2>&1
}

mkdir -p /data/adb
: > "$LOG"
log_msg "Fire AOD System Fix start"
wait_for_boot
apply_aod_settings
apply_overlays
dump_state
log_msg "Fire AOD System Fix done"
