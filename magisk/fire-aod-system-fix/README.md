# Fire AOD System Fix

Author: `1VicTim1 <122aaa121@gmail.com>`

This Magisk/KSU module is a best-effort workaround for Xiaomi fire AOD framework configuration.

What it does:

- Installs a static framework RRO at `/system/product/overlay/FireAodFrameworkOverlay/FireAodFrameworkOverlay.apk`.
- Enables AOD-related settings: `doze_enabled`, `doze_always_on`, `doze_on_charge`, `ambient_enabled`.
- Creates fabricated framework overlays at boot as a fallback for:
  - `android:string/config_dozeComponent`
  - `android:bool/config_dozeAlwaysOnDisplayAvailable`
  - `android:bool/config_displayBlanksAfterDoze`
  - `android:integer/config_screenBrightnessDoze`
  - `android:dimen/config_screenBrightnessDozeFloat`
- Points `config_dozeComponent` to the installed SystemUI Doze service:
  `com.android.systemui/.doze.DozeService`.

Important limitation:

The collected logs showed `getDozeComponent()=null`, `Display State=OFF`, `mDefaultDozeBrightness=0.0`, and a fully black screencap while the kernel already entered panel AOD and set LM3697 AOD brightness. The installed SystemUI APK contains `com.android.systemui.doze.DozeService`, so the immediate framework problem is that the device overlay never points `config_dozeComponent` at it and leaves doze brightness float at zero.

How to verify after reboot:

```sh
su -c 'cmd overlay lookup android android:string/config_dozeComponent'
su -c 'cmd overlay lookup android android:dimen/config_screenBrightnessDozeFloat'
su -c 'dumpsys dreams | grep getDozeComponent'
su -c 'dumpsys display | grep -E "Display State|mDozeStateOverride|mScreenState"'
su -c 'cat /data/adb/fire-aod-system-fix.log'
```

Expected for a working system-side AOD:

- `getDozeComponent()` is not `null`.
- Display state becomes `DOZE` or `DOZE_SUSPEND` when AOD is active, not plain `OFF`.
- `screencap` during AOD is not fully black.

What to fix in ROM sources:

1. Framework/device overlay:

   In `device/xiaomi/fire/overlay/FrameworksResOverlayFire/res/values/config.xml`, add or fix:

   Required resources:

   ```xml
   <string name="config_dozeComponent" translatable="false">com.android.systemui/.doze.DozeService</string>
   <bool name="config_dozeAlwaysOnDisplayAvailable">true</bool>
   <bool name="config_displayBlanksAfterDoze">false</bool>
   <integer name="config_screenBrightnessDoze">102</integer>
   <item name="config_screenBrightnessDozeFloat" format="float" type="dimen">0.4</item>
   ```

2. SystemUI or Axion QuickLook:

   The component from `config_dozeComponent` must exist and must be a real dozing dream service.
   The tested `SystemUI.apk` already declares `com.android.systemui.doze.DozeService` with
   `android.permission.BIND_DREAM_SERVICE`, so do not point fire at `LowLightClockDreamService`.

   The service must:

   - Be declared with `android.permission.BIND_DREAM_SERVICE`.
   - Call the doze APIs so Power/DisplayManager requests `DOZE` or `DOZE_SUSPEND`, not normal `OFF`.

3. Device tree/product packages:

   `device.mk` already includes the overlay package:

   ```make
   PRODUCT_PACKAGES += \
       FrameworksResOverlayFire
   ```

4. Kernel boundary:

   Do not use `FB_BLANK_POWERDOWN` as proof of AOD. Recovery also uses normal powerdown, so kernel AOD should only happen from real AOD blank states or explicit `MTKFB_SET_AOD_POWER_MODE`.
