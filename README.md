# salvage-exfat
Utility to search and recover files from a corrupt exFAT volume.

Usage:
`sef [-s] [-r] [-d rescue_dir] image_file`

This utility will search entire disk for directory entries and attempt to save file contents. Requires valid boot sector to detect disk geometry. If the media has read errors (and generally), it is recommended to perform the search on a disk image. A copy of the corrupt media can be dome using utility such as [ddrescue](https://www.gnu.org/software/ddrescue/) or `dd` (when media reads without errors).

Rescued files will be created in the directory specifie by -d switch, each file name will have a suffix which indicated first cluster of the file data.

The source will be open read-only.
