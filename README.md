libut: A Library of User Threads
================================

This library is extracted from [Shenango](https://github.com/shenango/shenango)
for its light-weight user space thread design only.
Most source files are simply copied from Shenango with few minor modifications:
 - Replaced tabs with spaces
 - Removed the code that is unnecessary for the basic threading
 - Renamed some file and function names to match the simplified functionality
 - Wrote the aarch64 counterparts for the x86\_64 assembly code
