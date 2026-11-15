#include <lykoware/ui.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace lykoware::ui {
namespace {
void check(bool valid)
{
    if (!valid) {
        throw std::invalid_argument("invalid UI topology, font, settings or layout range");
    }
}
}
std::size_t Tree::add(Node node)
{
    check((!node.parent || *node.parent < nodes_.size()) && std::isfinite(node.x)
          && std::isfinite(node.y) && std::isfinite(node.wrap_width) && node.wrap_width >= 0);
    nodes_.push_back(std::move(node));
    return nodes_.size() - 1;
}
Layout Tree::layout(const Catalog& catalog, const Font& font, const Settings& settings) const
{
    check(std::isfinite(settings.text_scale) && settings.text_scale > 0);
    const double advance = font.advance * settings.text_scale;
    const double height = font.line_height * settings.text_scale;
    check(std::isfinite(advance) && advance > 0 && std::isfinite(height) && height > 0
          && font.glyphs.contains(U'?'));
    for (const auto& [codepoint, uv] : font.glyphs) {
        (void)codepoint;
        check(std::isfinite(uv.x) && std::isfinite(uv.y) && std::isfinite(uv.width)
              && std::isfinite(uv.height) && uv.x >= 0 && uv.y >= 0 && uv.width > 0
              && uv.height > 0 && uv.x + uv.width <= 1 && uv.y + uv.height <= 1);
    }
    Layout result;
    result.reduced_motion = settings.reduced_motion;
    const auto locale = catalog.find(settings.locale);
    for (const auto& node : nodes_) {
        Rect bounds{node.x, node.y, 0, 0};
        if (node.parent) {
            bounds.x += result.nodes[*node.parent].x;
            bounds.y += result.nodes[*node.parent].y;
        }
        check(std::isfinite(bounds.x) && std::isfinite(bounds.y));
        std::u32string text;
        if (!node.key.empty()) {
            if (locale != catalog.end() && locale->second.contains(node.key)) {
                text = locale->second.at(node.key);
            } else {
                result.diagnostics.push_back("missing string: " + settings.locale + "/" + node.key);
                text = U"?";
            }
        }
        double x = 0, y = 0;
        if (!text.empty()) {
            bounds.height = height;
        }
        for (char32_t codepoint : text) {
            if (codepoint == U'\n') {
                x = 0;
                y += height;
                bounds.height = y + height;
                check(std::isfinite(bounds.height));
                continue;
            }
            if (node.wrap_width > 0 && x > 0 && x + advance > node.wrap_width) {
                x = 0;
                y += height;
            }
            auto glyph = font.glyphs.find(codepoint);
            if (glyph == font.glyphs.end()) {
                result.diagnostics.push_back("missing glyph: " + node.key);
                codepoint = U'?';
                glyph = font.glyphs.find(codepoint);
            }
            Rect quad{bounds.x + x, bounds.y + y, advance, height};
            x += advance;
            bounds.width = std::max(bounds.width, x);
            bounds.height = y + height;
            check(std::isfinite(quad.x + advance) && std::isfinite(quad.y + height)
                  && std::isfinite(bounds.width) && std::isfinite(bounds.height));
            result.glyphs.push_back({result.nodes.size(), codepoint, quad, glyph->second});
        }
        result.nodes.push_back(bounds);
    }
    return result;
}
}
