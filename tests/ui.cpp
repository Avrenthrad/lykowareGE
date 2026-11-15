#include <lykoware/ui.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}
template<class F> void rejects(F action)
{
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid UI input refused");
}
}
int main()
{
    try {
        using namespace lykoware::ui;
        Font font{8, 12, {}};
        for (char32_t c : U"?HelloSalut ") {
            font.glyphs[c] = {0, 0, 0.1, 0.2};
        }
        Catalog catalog{{"en", {{"greeting", U"Hello"}}}, {"fr", {{"greeting", U"Salut"}}}};
        Tree tree;
        const auto root = tree.add({{}, "", 10, 20, 0});
        const auto label = tree.add({root, "greeting", 2, 3, 24});
        auto english = tree.layout(catalog, font, {"en", 1, false});
        require(english.nodes.size() == 2 && english.glyphs.size() == 5 && english.diagnostics.empty(), "query tree");
        require(english.nodes[label].x == 12 && english.nodes[label].y == 23
                && english.nodes[label].width == 24 && english.nodes[label].height == 24, "parent offset and wrap");
        require(english.glyphs[3].bounds.x == 12 && english.glyphs[3].bounds.y == 35
                && english.glyphs[3].node == label && english.glyphs[0].uv.width == 0.1, "drawable quads");
        auto french = tree.layout(catalog, font, {"fr", 2, true});
        require(french.glyphs[0].codepoint == U'S' && french.glyphs[0].bounds.width == 16
                && french.nodes[label].height == 120 && french.reduced_motion, "locale and accessibility");
        require(english.glyphs[0].codepoint == U'H' && !english.reduced_motion, "old layout owns its data");
        auto missing = tree.layout(catalog, font, {"absent", 1, false});
        require(missing.diagnostics.size() == 1 && missing.glyphs[0].codepoint == U'?', "missing locale");
        catalog["en"].erase("greeting");
        require(tree.layout(catalog, font, {"en", 1, false}).diagnostics.size() == 1, "missing key");
        catalog["en"]["greeting"] = U"H\n\U0001F600";
        auto fallback = tree.layout(catalog, font, {"en", 1, false});
        require(fallback.glyphs.size() == 2 && fallback.glyphs[1].codepoint == U'?'
                && fallback.glyphs[1].bounds.y == 35 && fallback.diagnostics.size() == 1, "newline and unknown glyph");
        catalog["en"]["greeting"] = U"";
        require(tree.layout(catalog, font, {"en", 1, false}).nodes[label].height == 0, "empty text");
        rejects([&] { (void)tree.add({999, "", 0, 0, 0}); });
        rejects([&] { (void)tree.add({{}, "", 0, 0, -1}); });
        rejects([&] { (void)tree.layout(catalog, font, {"en", 0, false}); });
        rejects([&] { (void)tree.layout(catalog, font, {"en", std::numeric_limits<double>::infinity(), false}); });
        font.glyphs[U'?'].x = 2;
        rejects([&] { (void)tree.layout(catalog, font, {"en", 1, false}); });
        font.glyphs.erase(U'?');
        rejects([&] { (void)tree.layout(catalog, font, {"en", 1, false}); });
        std::cout << "UI foundation passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
