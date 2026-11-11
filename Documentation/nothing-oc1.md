# Nothing kernel OC1 — working CPU-OC baseline, experimental

Targets: CPUs 0–5 **2112 MHz**, CPUs 6–7 **2592 MHz**. The device binds
`qcom,clk-cpu-osm-sdmmagpie`; this is not the Linux Lucid CPU PLL path.

The owner reports that OC1 boots and that **2112 MHz little / 2592 MHz big**
work and are selectable in Franco Kernel Manager. This is the known-good CPU-OC
baseline, tagged `nothing-oc1-working`. Independent PLL/rail measurements,
long-term stability and ageing remain unverified. No voltage or thermal policy
is changed when publishing this state.

## What changes

- `CONFIG_NOTHING_OC=y` alone compiles the new code. Its OC-only DT properties
  are actually consumed by the driver; the legacy DT frequency lists stay stock.
- Probe attempts two unused OSM tail rows after verifying stock frequencies,
  source/divider/core/row metadata, matching duplicates and inactive requests.
  Voltage readback is checked before writing the L multiplier. OVERRIDE and
  firmware CPR corners are never programmed; no stock FREQ/VOLT row is written.
- The former fixed **+32 mV** is removed. The new open-loop increment extrapolates
  the last stock voltage/frequency slope and bounds it to **+4–8 mV**; a flat
  stock slope uses +4 mV. 1300 mV is a malformed-data guard, **not a proven safe
  rail voltage**. CPR may ignore or compensate the open-loop request.
- Rejected writes, changed metadata, failed tail restoration or later firmware
  revocation retain a diagnostic software OC entry but permanently restrict
  hardware requests to the saved stock row. Fast and slow CPUFreq accounting
  report the effective request, not a synthetic OC rate. `.get` decodes the
  hardware desired row, which still is **not a physical PLL measurement**.
- The CPU PLL maximum/lock interface is not exposed by this OSM path. Boot logs
  explicitly say **unknown** rather than using unrelated GPU/GCC/Lucid limits.
- Boot `pr_info` messages tagged `Nothing-OC:` dump every firmware LUT row,
  post-attempt rows, FREQ/VOLT/OVERRIDE readback and every final CPUFreq entry.
  OC builds enlarge the log ring from 128 KiB to 1 MiB to help retain these logs.

## Thermal gate

Poll existing `cpu-0-max-step`, `cpu-1-max-step` and `quiet_therm` every 100 ms.
The last zone is a **board/skin proxy**, not a direct skin-temperature measurement.
Each cluster is capped at its stock maximum at CPU >=70°C or proxy >=42°C.
After a cap, allow OC only at CPU <=60°C and proxy <=39°C. Missing/invalid
readings, a sample older than 500 ms and suspend also block OC. A worker updates
CPUFreq policy limits; both request paths independently enforce the gate and
recheck the programmed rows. Existing LMH, thermal cooling and userspace caps
are retained and may impose lower limits. Polling is not a hard-real-time
temperature guarantee and is not a replacement for firmware protection.

## Risks / limits

LUT writes can be rejected, restored by firmware, or cause boot failure. PLL
lock, voltage margin, CPR behavior and stability are untested. Instability can
cause hangs, crashes, reboots and data corruption. Higher power/voltage can
increase heat, battery stress and hardware ageing. Existing scheduler/thermal
power estimates are not calibrated for OC; no guessed energy-cost values or
thermal/firmware bypasses are added. Readback, an advertised frequency and
`time_in_state` are **not physical OC proof**.

## Build / preserve NoOC

The working bootfix2 NoOC `Image.gz`, base DTB and ROM-specific DTBO are reused
byte-for-byte. Their embedded release remains bootfix2. Only OC is rebuilt with
`CONFIG_LOCALVERSION="-Nothing-oc1"` and automatic SCM suffixes disabled. New
installers all print `ERNOBUILDS` and retain `kernel.string=Nothing kernel`.
Original bootfix2 releases, existing `out/` and `drivers/kernelsu` are untouched.

`arch/arm64/configs/nothing_oc1_defconfig` captures the exact resolved OC1 build
configuration in source form. From a fresh checkout, using AOSP clang r450784d:

```sh
LOCALVERSION= OUT=out-oc1-rebuild bash scripts/build-nothing.sh nothing_oc1_defconfig
python3 scripts/test-nothing-oc.py --output /path/to/validation/oc1-tests
LOCALVERSION= OUT=out-oc1-rebuild bash scripts/build-nothing.sh drivers/clk/qcom/clk-cpu-osm.o dtb.img dtbo.img
LOCALVERSION= OUT=out-oc1-rebuild bash scripts/build-nothing-variants.sh /path/to/oc1/artifacts/oc
python3 scripts/package-nothing.py \
  --template /path/to/pinned/AnyKernel3 \
  --oc-artifacts /path/to/oc1/artifacts/oc \
  --nooc-artifacts /path/to/preserved/nooc/artifacts \
  --output /path/to/oc1 --date 20261111 --revision oc1
```

Tests execute the actual extracted driver functions with mock MMIO, thermal
inputs, write rejection, partial rollback, revocation and truthful request
accounting under native ASan/UBSan and AArch64 user-mode QEMU. This is **not**
emulation of a sweet phone boot, its PLL, firmware or physical temperatures.

## Collect immediately after your manual flash / boot

```sh
adb exec-out 'su -c "dmesg | grep Nothing-OC"'
adb exec-out 'su -c "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies"'
adb exec-out 'su -c "cat /sys/devices/system/cpu/cpu6/cpufreq/scaling_available_frequencies"'
adb exec-out 'su -c "uname -r; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq /sys/devices/system/cpu/cpu6/cpufreq/scaling_max_freq"'
```

Use OEM-OC first for HyperOS, as planned. If it fails, manually restore the
original working bootfix2 NoOC ZIP. The build agent does not flash, reboot,
write device settings, stress the phone, or access data/backups.
