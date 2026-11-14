# ADR 0082: Stable asset IDs and source-free cooked packages

Status: Accepted foundation for 07.01. Owner: GPT.

Asset IDs are case-sensitive stable UTF-8 strings selected by the author, not file
paths or runtime pointers. Nonempty IDs up to 256 bytes are unique in a package.
The standalone CLI `python3 tools/assets/cook.py MANIFEST OUTPUT` is GPT-owned;
it does not alter Grok's lyko CLI files. A version-1 JSON manifest lists each ID,
its source path relative to the manifest, and dependency IDs. Sources must remain
inside the manifest directory, including symlink resolution. Missing sources,
duplicate IDs, missing dependencies and cycles fail the cook without replacing
the last good output. No dependency or service is added.

The package is versioned canonical JSON sorted by ID, with hex payload bytes,
a 64-bit FNV-1a content fingerprint and expected content fingerprints for direct
dependencies. FNV is an accidental-change check, not a security signature. Source
paths are omitted. The runtime opens the package using an explicit byte limit;
it needs no manifest/source tree. It verifies every payload, dependency reference,
expected dependency fingerprint and acyclic graph before replacing its output.
A missing or stale dependency fails the whole load. Packages are immutable:
changing an embedded dependency while retaining its old record/fingerprint fails;
splicing in a newly cooked dependency fails its dependents' expected fingerprints.
An old intact package remains valid when external sources change: a source-free
runtime cannot detect external edits. Re-cook before distributing changed sources.

Package owns IDs, payloads and edges; returned lookups borrow until package
mutation/destruction. Parsing/loading is single-threaded per output object;
immutable packages can be read concurrently. Malformed/oversized input and IO
failures return false with a diagnostic and leave output unchanged. Allocation
errors propagate. The cook sorts all entries and dependency maps, so unchanged
input cooks byte-identically. This is a byte-package foundation, not a new mesh,
shader or image importer; accepted importer ownership stays as assigned.

07.02 extends the same cook CLI: identical canonical output is reported as
`unchanged` and is not opened for writing. Changed content reports `rebuilt`.
Optional `--report PATH` writes a JSON success report atomically; it cannot alias
manifest, sources or package. Failures print JSON `{status, diagnostic}` to stdout
and a concise diagnostic to stderr; no failure report file is written because
path safety cannot be established for an invalid manifest. Missing source errors
include asset ID and authored path. Incremental means no unchanged output rewrite;
inputs are still read/validated, not a persistent timestamp-based importer cache.
