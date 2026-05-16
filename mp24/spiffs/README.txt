mp24 SPIFFS source directory
============================

Every file in this directory is packaged into the `spiffs` partition
(see ../partitions.csv: 1984 KB at 0x210000) at build time via the
`spiffs_create_partition_image` call in main/CMakeLists.txt, and is
made available to the running firmware under the `/spiffs` VFS root.

Place runtime assets here:
  - intro.gif       — boot animation (CircuitOS FSLVGL consumes this)
  - splash.bin      — title-card bitmap
  - avatars/*.gif   — pet/friend graphics
  - sfx/*.raw       — sound effects (16-bit signed PCM @ 22.05 kHz
                      to match audio_i2s.c's stream format)

For S-MP07 bring-up this directory only contains README.txt (this
file) and hello.txt (a known-content file the firmware reads back at
boot to verify SPIFFS mounted correctly). The real CircuitOS-port
assets get dropped in alongside the C++ shim work in a later session.
