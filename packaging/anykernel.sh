### AnyKernel3 Ramdisk Mod Script
## osm0sis @ xda-developers

### AnyKernel setup
properties() { '
kernel.string=Nothing kernel
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=sweet
device.name2=sweet_k6a
device.name3=sweet2
supported.versions=11 - 17
supported.patchlevels=
supported.vendorpatchlevels=
'; }

boot_attributes() {
  set_perm_recursive 0 0 755 644 $RAMDISK/*;
  set_perm_recursive 0 0 750 750 $RAMDISK/init* $RAMDISK/sbin;
}

BLOCK=/dev/block/bootdevice/by-name/boot;
IS_SLOT_DEVICE=0;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

# Set by scripts/package-nothing.py for each release ZIP.
NOTHING_OC=1;
NOTHING_VARIANT=AOSP;

. tools/ak3-core.sh;
. "$BIN/nothing-cmdline.sh";

dump_boot;
nothing_patch_cmdline "$NOTHING_OC";
ui_print ERNOBUILDS;
ui_print "Nothing kernel - $NOTHING_VARIANT";
if [ "$NOTHING_OC" = 1 ]; then
  ui_print "EXPERIMENTAL: little 2112 MHz / big 2592 MHz requested";
  ui_print "LUT/PLL success is UNVERIFIED; stock fallback is enabled";
else
  ui_print "CPU OC disabled: stock firmware frequencies";
fi;
write_boot;
