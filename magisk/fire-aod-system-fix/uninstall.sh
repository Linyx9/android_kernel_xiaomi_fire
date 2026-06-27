#!/system/bin/sh

LOG=/data/adb/fire-aod-system-fix-uninstall.log

for name in \
  FireAodDozeComponent \
  FireAodAlwaysOnAvailable \
  FireAodDisplayBlanksAfterDoze \
  FireAodScreenBrightnessDoze \
  FireAodScreenBrightnessDozeFloat; do
  cmd overlay disable --user 0 "com.android.shell:${name}" >> "$LOG" 2>&1
done

echo "Fire AOD System Fix overlays disabled" >> "$LOG"
