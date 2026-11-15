# Update 10.7 — HiBy R1 CPU power checks

The CPU diagnostics in Settings > System > Developer options > Processes read kernel interfaces only. They do not set a frequency, governor, voltage, process priority or thermal limit. Current clock policy is intentionally unchanged until a real R1 supplies capability data. Unsupported values are shown as unavailable.

The Processes page no longer scans processes or allocates the process rows during startup. It rebuilds on page entry or manual refresh and frees rows on exit. CPU sampling runs every two seconds while the page is displayed; the timer is paused when leaving the page and registered with display standby. Diagnostics labels currently use English.

## Read-only hardware verification

With USB ADB already working, run these individual commands from a computer:

```sh
adb shell cat /proc/cpuinfo
adb shell cat /proc/stat
adb shell ls /sys/devices/system/cpu/cpufreq
adb shell ls /sys/devices/system/cpu/cpu0/cpufreq
adb shell cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
adb shell cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors
adb shell cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies
adb shell cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
adb shell cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies
adb shell ls /sys/class/thermal
```

Missing paths are acceptable; do not create them, adjust clocks, or patch the kernel. Do not infer a CPU temperature from an unidentified sensor.

## Validation before enabling any future clock policy

- Compile host and R1 builds; retain existing ABI and pinned-rootfs checks.
- On the device, confirm the diagnostic panel shows plausible data or explicitly unavailable values, refresh manually and leave the page; confirm it stops sampling when hidden.
- Compare CPU utilization and battery drain on the same brightness and network settings for static reading, music, paused playback and gaming.
- Confirm normal wake/suspend, ADB, USB and gapless playback.
- Any future governor adjustment needs a supported kernel interface, actual R1 measurements, an explicit opt-in and preserved recovery path. No frequency policy is introduced in this phase.

This branch has not been hardware validated. The diagnostic screen itself consumes CPU when open; close it for battery measurements.
