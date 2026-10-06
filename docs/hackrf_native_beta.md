# HackRF native RX beta

FobosAPP contains an experimental native HackRF receive backend. It does not
use SoapySDR. The application loads `libhackrf` dynamically and converts the
native signed 8-bit interleaved IQ stream into the shared FobosAPP float-IQ
pipeline used by the spectrum, waterfall, demodulators, recording and network
server.

## Runtime

FobosAPP searches for the Windows runtime in this order:

- `hackrf/hackrf.dll`
- `hackrf/libhackrf.dll`
- `hackrf.dll` or `libhackrf.dll` next to `FobosAPP.exe`
- the system library search path

On Linux, install the distribution's `libhackrf` runtime. The source build does
not require HackRF development headers because the backend resolves the stable
C API at runtime.

Official software and installation instructions:

- https://github.com/greatscottgadgets/hackrf
- https://hackrf.readthedocs.io/

## Initial implementation

- native asynchronous RX via `hackrf_start_rx`;
- signed 8-bit IQ conversion into the shared float-IQ pipeline;
- 2, 4, 8, 10, 12.5, 16 and 20 MS/s presets;
- baseband-filter selection derived from the sample rate;
- live center-frequency retuning and Standard scan support;
- conservative initial gains: LNA 16 dB, VGA 20 dB, RF amp off;
- bias tee off by default;
- local and server-provided receiver selection.

This path is RX-only and unverified until real HackRF hardware is available.
Multiple-device selection, device serial reporting, dedicated gain controls and
transmission are intentionally left for hardware-backed testing. Transmission
must later be implemented as a separate explicitly armed workflow with device
and regulatory safety checks.
