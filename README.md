# salvage-exfat
Utility to search and recover files from a corrupt exFAT volume.

Usage:
`sef [-s] [-r] [-d rescue_dir] [-b beginning_cluster] [-e end_cluster] image_file`

This utility will search entire (or part of the) disk for directory entries and attempt to save file contents. Requires valid boot sector to detect disk geometry. If the media has read errors (and in general), it is recommended to perform the search on the disk image rather than the device. A copy of the corrupt media can be created using utility such as [ddrescue](https://www.gnu.org/software/ddrescue/) or `dd` (only when media has no read errors).

Rescued files will be created in the directory specified by -d switch (default: `rescue.dir`), each file name will have a suffix which indicates the first cluster of the file data.

The source will be open read-only.
