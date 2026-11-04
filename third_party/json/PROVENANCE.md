# nlohmann/json

- Upstream: https://github.com/nlohmann/json
- Version: v3.11.3, release single-header source.
- Included: `single_include/nlohmann/json.hpp` as `include/nlohmann/json.hpp`; upstream `LICENSE.MIT` unchanged.
- Header SHA-256: `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`.
- Licence: MIT; copyright Niels Lohmann, preserved in header and licence.
- No transitive runtime dependencies; C++ standard library only. No local source patches.
- Private implementation dependency of `lykoware::scene`; no JSON-library types in public APIs.
- Updates: pin a new release/commit, verify licence and hash, replace source, run malformed-input, canonical round-trip and replay tests.
