# GNU Radio Compatibility Plan

## Decision

Obrii SDR should interoperate with GNU Radio through an optional external
bridge. GNU Radio must not be linked into the main executable and its scheduler
must not sit between a Fobos callback and IqBuffer.

Reasons:

1. GNU Radio blocks depend on the GNU Radio runtime ABI, scheduler, PMT types,
   and often Python/GRC. Arbitrary installed OOT binaries cannot be loaded as
   stable Obrii plug-ins.
2. The current stable GNU Radio line is 3.10, while GNU Radio 4.0 is a separate
   active-development project. Keeping it external isolates version changes.
3. Fobos can produce 50-80 MS/s. Additional generic queues and format
   conversions in the core path would work against Obrii's main performance
   goal, especially on Raspberry hardware.

Official references:

- GNU Radio repository and built-in module layout:
  https://github.com/gnuradio/gnuradio
- Current GNU Radio releases:
  https://github.com/gnuradio/gnuradio/releases
- Out-of-tree module model:
  https://wiki.gnuradio.org/index.php/OutOfTreeModules
- ZeroMQ stream and message blocks:
  https://wiki.gnuradio.org/index.php/Understanding_ZMQ_Blocks
- Stream tags:
  https://wiki.gnuradio.org/index.php/Stream_Tags
- Tagged streams and PDUs:
  https://wiki.gnuradio.org/index.php/Tagged_Stream_Blocks

## Existing Obrii Transport

Obrii's desktop network mode already emits framed binary IQ plus JSON
metadata:

- full IQ: iq_s8_interleaved;
- channel IQ: channel_iq_s16le;
- metadata: sequence, sample rate, source sample rate, sample count, center and
  listening frequency, bandwidth, modulation, input mode, and channelized flag.

This is sufficient for a first receive-only adapter without touching device
backends or the hot IQ publication path.

## Proposed Components

### obrii-gr-bridge

A separate process that:

1. Connects to Obrii network mode and validates sequence/format metadata.
2. Converts packed Obrii IQ to GNU gr_complex only in the bridge process.
3. Publishes samples through a GNU-compatible ZeroMQ stream endpoint.
4. Publishes metadata and discontinuity events on a separate message endpoint.
5. Applies bounded queues and drops stale frames instead of blocking Obrii.

Channel IQ is the default transport. Full 50-80 MS/s IQ is explicitly
experimental over TCP/ZeroMQ; a local shared-memory ring is the later path for
that rate.

### gr-obrii OOT Module

Build separately against a declared GNU Radio 3.10 maintenance version:

1. Obrii IQ Source: complex-float stream plus rx_rate, rx_freq,
   sequence, and discontinuity tags.
2. Obrii Control: PMT/PDU messages to a constrained JSON command set.
3. Obrii IQ Sink: optional processed-IQ return path, disabled initially.
4. Obrii Audio Source/Sink: optional 48 kHz audio integration after IQ source
   timing is validated.

The OOT package owns all GNU headers, libraries, YAML block definitions, and
Python bindings. The Obrii package remains runnable without GNU Radio.

## Compatibility Phases

### Phase 1: Receive-only channel IQ

- Adapter consumes channel_iq_s16le.
- GNU Radio receives complex float and immutable per-session rate/frequency
  metadata.
- Sequence gaps become explicit tags/messages.
- No GNU command can retune or start/stop hardware.

### Phase 2: Controlled commands

- Add allow-listed start, stop, center frequency, listening frequency,
  bandwidth, modulation, and gain commands.
- Commands carry a request ID and receive success/error acknowledgement.
- Obrii remains the owner of device validation, frequency limits, and state.

### Phase 3: Local wideband transport

- Add an optional shared-memory ring with generation, sequence, timestamp,
  format, sample-rate, and overrun fields.
- Benchmark against direct Obrii processing at 20, 50, and 80 MS/s.
- Keep the feature disabled when its measured cost is not acceptable.

### Phase 4: Return paths

- Accept processed channel IQ or audio from GNU Radio into a new bounded input.
- Never overwrite or mutate the primary receiver IqBuffer.
- Expose returned streams as explicit DSP-board sources.

## What Not To Implement

1. Do not scan the system for arbitrary GNU block libraries and load them into
   Obrii.
2. Do not embed the GNU scheduler in the receive process merely to gain access
   to OOT blocks.
3. Do not advertise zero-copy full-rate transport until measured on Fobos and
   Raspberry hardware.
4. Do not let an external flowgraph directly call receiver DLL handles.

## Acceptance Tests

1. Known complex tone arrives with correct polarity, rate, frequency, and
   amplitude.
2. Retune produces one metadata transition and no mixed old/new stream.
3. Deliberate adapter slowdown produces an explicit gap, not unbounded latency.
4. Channel IQ audio from GNU Radio matches Obrii replay duration and pitch.
5. With the bridge disabled, Obrii performance and dependencies are unchanged.
