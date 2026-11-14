# ADR 0083: Bounded PCM streaming and nearest voice admission

Status: Proposed. Item: 07.07. Owner: GPT.

ADRs 0032–0034 leave long-clip disk streaming and full-mixer voice priority open.
The existing mixer, device output and spatial attenuation remain the foundation.

## Decision

`PcmStream` reads raw stereo 48 kHz signed 16-bit little-endian PCM. This is an
explicit runtime input contract, not a compressed decoder or a WAV importer.
The caller supplies a nonzero ring capacity (at most 48,000 frames) and explicitly
pumps disk data before rendering. Only `pump` performs disk reads; rendering
consumes buffered frames without loading the whole file. Starvation emits silence
without consuming source frames; pumping can resume playback. EOF drains buffered
frames, then ends the voice. Cancellation closes the file and releases the ring.
The stream owns its file and ring; a mixer borrows shared ownership while playing.
One stream can feed only one voice; it is single-use and cannot loop or resample.

All stream/mixer calls are serialized on one thread, or through Output's mixer
lock. Pumping can block: callers must prefill before resume and schedule refills
outside the device callback. This is bounded pull streaming, not asynchronous I/O;
no claim of glitch-free target-PC playback is made. Joshua runs this on the RTX
4070 Windows PC for listening and scheduling measurements.

`Mixer::play_nearest` admits a finite nonnegative listener distance. At saturation,
a strictly nearer voice replaces the farthest prioritized voice (lowest slot wins
an equal-distance victim tie); equal or farther arrivals are refused. Legacy
`play` voices and streams are protected. Replaced handles become stale. Spatial
play/update supplies distances from double listener-relative positions. Admission
is deterministic for the same ordered calls; this is not virtualized playback.

Failures return empty/false and an error; allocation failures may propagate.
Invalid input cannot evict a voice. A truncated/read-failed PCM stream cancels.
No dependency, codec, device setting, effects bus or environment effect is added.

## Verification

Headless tests fill all voice slots, retain nearer arrivals, reject farther and
invalid arrivals, invalidate stolen handles and exercise spatial admission.
A long generated clip is consumed through a small ring and compared to known
samples across refill boundaries. Tests cover starvation, EOF, duplicate playback,
invalid capacity/format, cancellation and buffer release. A deliberate corrupted
near-distance or cancellation result must fail the same assertions.
