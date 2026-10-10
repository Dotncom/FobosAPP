# DSP Board Coverage Audit

Audit scope: user-visible desktop functions in the current development tree.

## Direct Or Self-Contained Blocks

- Receiver/IQ/network/playback sources, start/stop, enable, center/listening
  frequency, fine tuning, channel filter, resampler, FFT, demodulator, audio
  output, recorder, replay, network output, transmitter, and GPIO.
- The receiver editor mirrors receiver/input/clock/sample-rate/FFT/gain controls
  and provides direct Fobos details and HackRF RX-settings actions.
- Full audio-filter chain plus one block per filter type.
- Full workspace spectrum, frequency ruler and waterfall blocks that consume the
  already prepared shared spectrum frame, plus settings blocks for the legacy
  main spectrum/2D/3D views, Multi-VFO channel, mini-spectrum and mini-waterfall.
- The board can become the main maximized workspace, use saved presets up to
  five columns, and split each column independently into 1-4 resizable sectors.
  Blocks and live visual widgets can be dragged across sector boundaries while
  retaining their links. The hidden standard window remains the owner of
  receiver, audio and DSP lifetime.
- Self-contained oscilloscope, constellation, eye diagram, and digital
  synchronization plots, each with its matching settings block.
- Digital audio/video settings and independent text/image result windows.

## Exact Tool Launchers

- Agile, standard, and listening scans; DMR/FPV/video hunters.
- Spectrum measurements, zoom spectrum, selected-band density, zero-span,
  spur suppression, spectrum recording, and replay.
- Interference/comb analysis, signal statistics, IQ diagnostics, dual-input HF
  analysis, scientific analyzer, density, masks/baseline, pulse analysis, and
  measurement sessions.
- DMR, CW, SSTV, digital video, GNSS over SDR, serial NMEA/UBX, QTH map,
  presets/calibration, and general settings.

## Intentional Boundary

The board mirrors controls and embeds selected displays, but it is not a
general-purpose runtime graph. A drawn ordinary signal connection does not
automatically replace Obrii's optimized IQ/audio path. Executable links today
are control links, VFO-channel-to-VFO-display links, and matching research
settings-to-research-view links. VFO views can alternatively store a direct VFO
number, and matching research pairs can share a binding ID, so long cross-sector
wires are optional while explicit local wires remain supported. Individual audio-filter blocks create real
AudioFilterChain stages, but stage order still comes from that chain's list,
not from canvas geometry. New executable routing is added only when a block has
a bounded real-time implementation and has been profiled on Raspberry-class
hardware.

The planned GNU Radio bridge is therefore an explicit external source/sink,
not a mechanism for silently loading arbitrary GNU blocks into this board.
