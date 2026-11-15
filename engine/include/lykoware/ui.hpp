#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lykoware::ui {
struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};
struct Font {
    double advance = 0, line_height = 0;
    std::map<char32_t, Rect> glyphs; // Normalised atlas UVs; '?' is required.
};
struct Settings {
    std::string locale;
    double text_scale = 1;
    bool reduced_motion = false;
};
using Catalog = std::map<std::string, std::map<std::string, std::u32string>>;
struct Node {
    std::optional<std::size_t> parent;
    std::string key; // Empty key is a container.
    double x = 0, y = 0, wrap_width = 0;
};
struct Glyph {
    std::size_t node = 0;
    char32_t codepoint = U'?';
    Rect bounds, uv;
};
struct Layout {
    std::vector<Rect> nodes;
    std::vector<Glyph> glyphs;
    std::vector<std::string> diagnostics;
    bool reduced_motion = false;
};
// GPT-owned UI foundation (ADR 0091). Tree owns nodes; inputs are borrowed only
// during layout. Output owns all bounds/glyphs/diagnostics and survives inputs.
// Single controlling thread; no concurrent mutation/layout. add refuses invalid
// parent/geometry with invalid_argument. layout similarly rejects invalid font,
// settings or overflow. Missing strings/glyphs report diagnostics and use '?'.
// Allocation failures propagate. No font asset or rasteriser is included.
class Tree {
public:
    [[nodiscard]] std::size_t add(Node node);
    [[nodiscard]] Layout layout(const Catalog& catalog, const Font& font, const Settings& settings) const;
private:
    std::vector<Node> nodes_;
};
}
