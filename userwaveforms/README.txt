Put PreenFM2 user waveforms here to enable OSC_SHAPE_USER1..USER6.

Expected files (either format works):
  - USR1.BIN .. USR6.BIN   (preferred)
  - usr1.txt  .. usr6.txt  (float sample list export)

BIN format:
  - 1024 samples
  - float32 little-endian
  - total size 4096 bytes (raw) or 4104 bytes (8-byte header)

TXT format:
  - 1024 float samples in [-1, 1]
  - samples separated by any whitespace/comma

Minimalith behavior:
  - If a preset references User1..User6 and the required file is missing, the preset is hard-muted.
