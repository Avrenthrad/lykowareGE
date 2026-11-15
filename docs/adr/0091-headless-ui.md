# ADR 0091: Headless UI tree and text layout (07.06)

Status: accepted. Owner: GPT.

The runtime owns a flat parent-before-child UI tree. Nodes contain a localisation
key, parent index, local offset and explicit wrap width (zero means no wrap).
Layout returns owned node bounds and glyph rectangles with atlas UVs, in insertion
order, so a renderer can consume them without retaining tree pointers. No renderer
or editor changes belong to this foundation.

Catalogues own UTF-32 strings indexed by locale and key. A missing locale/key
produces a diagnostic and visible `?`; a missing glyph uses the required `?` glyph
and reports a diagnostic. Empty keys are non-text containers. No implicit locale
fallback. Changing locale requires another layout; earlier output remains valid.

Font data is caller-supplied monospace metrics and a codepoint-to-atlas-UV map.
No font, font engine or other dependency is vendored. Callers must have rights to
their atlas. Tests use synthetic UVs, not a font asset. Newline and character wrap
are supported; proportional shaping, bidi, ligatures, rasterisation, clipping,
interaction and GPU atlas upload are future work. UTF-32 avoids an implicit UTF-8
parser; catalog importers must decode their input before calling this API.

Settings expose positive finite text scale and reduced-motion. Scale changes
metrics and wrap; offsets are UI coordinates. Reduced-motion is copied to output
for consumers; this module schedules no animation. All calls are serialised by
the owning simulation thread. Invalid topology, metrics, UVs or numeric overflow
throw invalid_argument; no partial layout escapes. Allocation errors propagate.
