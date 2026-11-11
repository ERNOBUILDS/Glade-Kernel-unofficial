# Nothing kernel for sweet

Performance/OC trial for Redmi Note 12 Pro 4G (`sweet`, model `2209116AG`,
Snapdragon 732G / SM7150). Source base:
`9e8cff64c0ceb518e29dd1e6697f10c6738d141f`.

## Source changes

- Preserve the One UI freeze fix: `/proc/vmallocinfo` iteration releases
  `vmap_purge_lock` in `s_stop()` as well as `vmap_area_lock`.
- Enable runtime-selected ARM64 LSE atomics. The connected device advertises
  the `atomics` CPU feature.
- Backport Arm's combined, optimized `memcpy`/`memmove` implementation from
  Linux v5.15, with the 4.14 entry and position-independent symbol conventions.
- Copy trusted PIL firmware metadata with `memcpy_toio()` and a write barrier.
  Its strongly ordered DMA buffer is Device memory on arm64; normal `memcpy`
  can fault on unaligned tail writes. This fixes the diagnosed IPA-loading panic
  in the first trial; see the bootfix1 notes below.
- Accumulate OSM/WALT cycle-counter deltas using unsigned subtraction, including
  the tick at the 32-bit wrap boundary.
- Repair the existing LZ4 1.10 backport's oversized stack use. HC optimal-parser
  scratch is part of each caller-owned stream workspace. Initialization and
  dictionary copies cover only persistent state. On ARM64 the HC workspace is
  327,784 bytes; allocation macros use `sizeof` the complete stream type.
  `LZ4_compress_fast()` and `LZ4_compress_destSize()` also take caller workspace.
  There were no in-tree callers of those two former stateless wrappers.
- Use `-Nothing-bootfix1` in the kernel release, `Nothing kernel` in the banners
  and installer, and `Nothing@NothingKernel` as build identity.

## Overclocking

The current working CPU-OC baseline is documented in [nothing-oc1.md](nothing-oc1.md)
and tagged `nothing-oc1-working`. The owner reports OC1 boots and both targets
work and are selectable in Franco Kernel Manager. This remains experimental.
The SDMMAGPIE OSM driver attempts unused firmware CPU LUT rows with PLL
multipliers 110 and 135, using the 19.2 MHz oscillator:

| Cluster | Observed stock maximum | Trial target |
| --- | --- | --- |
| CPUs 0–5 | 1,804.8 MHz | 2,112 MHz |
| CPUs 6–7 | 2,304 MHz | 2,592 MHz |

The former fixed **+32 mV** open-loop request was removed in OC1. It did not
prove a 32 mV increase in the actual CPR-controlled rail and was not justified
as a safe margin. OC1 uses a bounded stock-slope-derived **+4–8 mV** on new rows
only, retains stock voltages/corners and never writes OVERRIDE. Rejected or
revoked tail rows are quarantined and hardware requests are stock-clamped;
failed restoration does not abort driver initialization. Boot diagnostics use
`Nothing-OC:`. CPU/board-proxy temperature limits also cap requests at stock.

The earlier bootfix1/bootfix2 trial reported rejected writes and remained at
stock clocks; that historical result is not the current OC1 result. Readback,
available-frequency lists and `time_in_state` still do not independently prove
physical PLL frequency, closed-loop voltage or long-term stability.

`nothing.oc=0` disables the LUT extension. The values `off`, `no`, `disable`
and `disabled` also disable it. OC packages install `nothing.oc=1`; NoOC packages
install `nothing.oc=0`. Installers normalize old occurrences so subsequent
OC/NoOC installations select the requested mode reliably.

## ROM variants

Use **OEM** for HyperOS and One UI ports, and **AOSP** for AOSP-based ROMs.
Both variants use the same kernel binary and base DTB. Following this tree's
established build recipe, the two K6 panel overlays contain these values:

| Variant | Width property | Height property |
| --- | --- | --- |
| AOSP | 69 | 156 |
| OEM | 695 | 1546 |

The source panel files retain their original 69/154 values between builds.
The base DTB has MSM ID 365/0 and board ID 0/0. The single sweet overlay has
board ID 46/0.

HyperOS 4 / Android 17 is the captured device environment. One UI 9 / Android 17
is a target port. The first HyperOS trial panicked during IPA firmware loading.
Bootfix1 needs a new phone boot test; ROM compatibility and performance gains
are not established by the build checks.

## Bootfix1 diagnosis

The supplied `console-ramoops-0` is corrupted, but the surviving fault, matching
release disassembly and source identify an alignment abort around 30.3447 seconds:

- ESR `0x96000061`: same-EL data abort, write, alignment fault (`FSC=0x21`).
- `__memcpy+0x188/0x250`: `stp x14, x15, [x5, #-64]`.
- Caller: `pil_init_image_trusted+0x1a4/0x550`, on the
  `transport_power_mgmt` workqueue running `ipa3_load_ipa_fw`.
- `DMA_ATTR_STRONGLY_ORDERED` selects `MT_DEVICE_nGnRnE`; the optimized generic
  copy's tail store is not necessarily naturally aligned for that mapping.

The fix uses the existing arm64 I/O-copy helper, which aligns its wide accesses
and copies trailing bytes individually. The ordinary-memory copy optimization
remains available. The first release's NoOC ZIP contains the same faulty PIL
call site, so disabling OC alone does not correct this panic.

The saved `kmsg.txt` contains only 13 later userspace/charging messages and does
not include the crash. Recovery's pstore copy matches the supplied file exactly.

## Build and package

Toolchain: AOSP clang `r450784d` (14.0.6), LLVM integrated assembler and LLD.
Host tools include GCC/G++, GNU bc, make, Python 3, flex, bison, and the usual
kernel host development libraries. `scripts/build-nothing.sh` defaults to
`../nothing-toolchain/clang` and `../nothing-toolchain/host/bin`.

The provided `out/` is already configured with `sweet_defconfig`. For a fresh
checkout, configure it with `bash scripts/build-nothing.sh sweet_defconfig`.
This 4.14 tree can leave generated `.config` and `include/config/` in the source
directory during defconfig; the existing `.github/workflows/d-77.yml` documents
their cleanup before the real out-of-tree build.

```sh
bash scripts/build-nothing-variants.sh /path/to/release/artifacts

# Template: https://github.com/hrihq/Glade-AnyKernel3.git
# Revision: d93ab7bab62d7ecff359bfb9cc64dcbfb1afb017
python3 scripts/package-nothing.py \
  --template /path/to/AnyKernel3 \
  --artifacts /path/to/release/artifacts \
  --output /path/to/release \
  --date 20261110 --revision bootfix1
```

`TOOLCHAIN_ROOT`, `OUT` and `JOBS` can override the local build environment.
The variant builder restores the original panel files on exit. The packager
uses the pinned template's tracked files, normalizes ZIP executable permissions,
and writes four ZIPs plus `SHA256SUMS`.

## Validation history

The original optimization checks are recorded in the first release:

- 363,354 AArch64-emulated copy cases: alignments, overlapping ranges, tails,
  protected pages and return values.
- 74 mock-MMIO OC cases: mode selection, row metadata, guards, write rejection,
  rollback failure and counter wrap.
- 8,010 LZ4 cases: pre-change byte equality, independent userspace decoding,
  guarded buffers, all parser strategies, limited output and dictionaries.
  ASan/UBSan checked the host harness. All 12 HC levels and both workspace
  wrappers also passed on a guarded 16 KiB native thread stack.
- Final ARM64 disassembly shows HC entry stack frames of 528/560 bytes,
  replacing the previous 66,112/66,144-byte frames.
- 135 ARM BusyBox installer cmdline cases, including repeated OC/NoOC switches.
- Embedded kernel config, image branding, DTB/DTBO IDs and panel dimensions.
- ZIP payloads and permissions, and offline repack/unpack of all four variants
  using the captured header-v2 HyperOS boot image.

Bootfix1 additionally reproduces the exact `0x96000061` alignment abort at
`__memcpy+0x188` in ARM64 system emulation, using instructions extracted from the
released `vmlinux` and actual Device-nGnRnE translation attributes. The same copy
succeeds with a Normal-memory destination. The corrected kernel's
`__memcpy_toio` passes 70,668 Device-memory cases, including all source/destination
alignments 0..15, metadata-sized tails, page boundaries and copies around 1 MiB.
Each checks data and destination guards; the source is also checked unchanged.
The initial user-mode copy checks did not model Device-memory destinations.

The final PIL disassembly calls `__memcpy_toio` followed by `dsb st`. Embedded
configuration differs only in the release suffix; the KSU/SUSFS and LSE options
remain enabled. Both DT variants match the first release byte for byte.

## Inspect after installation

```sh
adb shell uname -r
adb shell cat /proc/cmdline
adb shell 'su -c "dmesg | grep nothing-oc"'
adb shell cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies
adb shell cat /sys/devices/system/cpu/cpufreq/policy6/scaling_available_frequencies
```

If OC prevents a successful boot, install the matching NoOC ZIP from recovery.
For full rollback, restore the boot and DTBO backups belonging to the currently
installed ROM. A HyperOS boot backup is specific to that HyperOS installation.

## Credits

Based on the Glade kernel tree and the user's One UI vmalloc fix. Memory-copy
code is from Arm/Linux v5.15. Packaging uses osm0sis's AnyKernel3 through the
device-specific template above. Original source licenses and copyright notices
are retained.
