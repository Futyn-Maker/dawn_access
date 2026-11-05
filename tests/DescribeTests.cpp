// ui/Describe: the words said of a control, of the prompts and of a tab, in both verbosity
// levels.
#include "Support.hpp"
#include "locale/Locale.hpp"
#include "ui/Describe.hpp"

using namespace da;
using tests::TempDir;
using tests::U8;

namespace
{
    void LoadEnglish(const TempDir& dir)
    {
        dir.Write("lang/en_US.ini", "[strings]\nui.type.selector=selector\nui.type.slider=slider\nui.type.checkbox=checkbox\nui.type.tab=tab\n"
                                    "ui.pos={0} of {1}\nui.state.disabled=unavailable\n");
        REQUIRE(locale::LoadTables((dir.Path() / "lang").wstring(), L"en_US"));
    }
}

TEST_CASE("Speak says label, kind, value, state and place in detail, and only label, value and tip briefly")
{
    TempDir dir;
    LoadEnglish(dir);
    ui::Description d;
    d.kind = ui::Kind::Selector;
    d.group = L"Subtitles";
    d.label = L"Size";
    d.value = L"Medium";
    d.state = L"unavailable";
    d.index = 3;
    d.count = 6;
    d.tip = L"Sets the size of the subtitles.";
    CHECK(U8(ui::Speak(d, true, true)) == "Subtitles. Size, selector, Medium, unavailable, 3 of 6. Sets the size of the subtitles.");
    CHECK(U8(ui::Speak(d, true, false)) == "Size, selector, Medium, unavailable, 3 of 6. Sets the size of the subtitles.");
    CHECK(U8(ui::Speak(d, false, true)) == "Subtitles. Size, Medium. Sets the size of the subtitles.");
}

TEST_CASE("A button has no kind word, and an unknown place is left out")
{
    TempDir dir;
    LoadEnglish(dir);
    ui::Description d;
    d.kind = ui::Kind::Button;
    d.label = L"New Game";
    CHECK(U8(ui::Speak(d, true, true)) == "New Game");
    d.index = 1;
    d.count = 5;
    d.tip = L"Start a new story";
    CHECK(U8(ui::Speak(d, true, false)) == "New Game, 1 of 5. Start a new story");
    CHECK(U8(ui::Speak(d, false, false)) == "New Game. Start a new story");
    // The only control of its kind has no place worth telling.
    d.count = 1;
    CHECK(U8(ui::Speak(d, true, false)) == "New Game. Start a new story");
}

TEST_CASE("SpeakPrompts pairs each label with its key and skips a prompt without a label")
{
    std::vector<ui::Prompt> prompts{{L"Select", L"Enter"}, {L"", L"F"}, {L"Back", L"Backspace"}, {L"PlayStation overlay", L""}};
    CHECK(U8(ui::SpeakPrompts(prompts)) == "Select: Enter, Back: Backspace, PlayStation overlay");
    CHECK(ui::SpeakPrompts({}).empty());
}

TEST_CASE("SpeakTab names the tab, with its kind and place in detail")
{
    TempDir dir;
    LoadEnglish(dir);
    CHECK(U8(ui::SpeakTab(L"Accessibility", 2, 5, true)) == "Accessibility, tab, 2 of 5");
    CHECK(U8(ui::SpeakTab(L"Accessibility", 2, 5, false)) == "Accessibility");
    CHECK(U8(ui::SpeakTab(L"Audio", 0, 0, true)) == "Audio, tab");
}

TEST_CASE("A control the game's narration names is said in its words, without the caption above, 2026-11-01")
{
    // The totems tab: a slot is a picture under its column's caption, and the game narrates it as
    // "Тотем смерти", "6", "Недоступно" (WBP_TotemSegment's hover). The caption said before the
    // game's words named the column twice: "Смерть. Тотем смерти, 6, Недоступно".
    TempDir dir;
    LoadEnglish(dir);
    ui::Description d;
    d.kind = ui::Kind::Button;
    d.group = L"Смерть";
    d.label = L"Тотем смерти, 6, Недоступно";
    d.index = 1;
    d.count = 6;
    d.narrated = true;
    CHECK(U8(ui::Speak(d, false, true)) == "Тотем смерти, 6, Недоступно");
    CHECK(U8(ui::Speak(d, true, true)) == "Тотем смерти, 6, Недоступно, 1 of 6");
    // A row named by its own words keeps the heading above it.
    d.narrated = false;
    d.label = L"Субтитры";
    CHECK(U8(ui::Speak(d, false, true)) == "Смерть. Субтитры");
}
