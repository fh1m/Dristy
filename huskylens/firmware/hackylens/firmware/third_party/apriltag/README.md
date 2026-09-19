# AprilTag detector subset

This directory contains the TAG36H11-only subset of the OpenMV AprilTag fork at commit `636b9ba14ba8485c9fc09590cd4ef53f11427966`.

Upstream: <https://github.com/openmv/apriltag>

The retained sources are distributed under the BSD 2-Clause license in `LICENSE.md`. Embedded configuration in `common/config.h` disables pthreads, image I/O, debug output, profiling, and tag families other than TAG36H11. `tools/build_firmware.py` stages this directory only when the APRILTAG app is enabled.
