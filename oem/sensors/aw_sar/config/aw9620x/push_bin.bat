adb wait-for-device
@adb root
adb wait-for-device
@adb remount

@adb push aw9620x_bt_0.bin /vendor/firmware
@adb push aw9620x_fw_0.bin /vendor/firmware

@adb push aw9620x_bt_1.bin /vendor/firmware
@adb push aw9620x_fw_1.bin /vendor/firmware

@adb push ./aw96205/aw9620x_reg_0.bin /vendor/firmware
@adb push ./aw96205/aw9620x_reg_1.bin /vendor/firmware
pause
