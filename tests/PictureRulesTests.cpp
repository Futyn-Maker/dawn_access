// features/PictureRules: which description a film, a photo or a thing in the hand is kept under,
// and when the camera's photos are described.
#include "Support.hpp"
#include "features/PictureRules.hpp"

#include <string>
#include <vector>

using namespace da;
using Keys = std::vector<std::wstring>;

TEST_CASE("pictures: a film is known by its file's name, without the folder or the extension")
{
    // The players' URLs as the game opens them (UBinkMediaPlayer::URL), and the totems' own names. The
    // censored cut keeps its own name: its words may differ from the film's.
    CHECK(picture::FilmName(L"./Movies/totem_death_1.bk2") == L"totem_death_1");
    CHECK(picture::FilmName(L"../../../Bates/Content/Movies/death_act8_4_josh_wendigo_ja.bk2") == L"death_act8_4_josh_wendigo_ja");
    CHECK(picture::FilmName(L"Movies/Totem_Death_5_JA.bk2") == L"totem_death_5_ja");
    CHECK(picture::FilmName(L"Movies\\projector_Wendigo.bk2") == L"projector_wendigo");
    CHECK(picture::FilmName(L"totem_hunger_5_1") == L"totem_hunger_5_1");
    CHECK(picture::FilmName(L"").empty());
}

TEST_CASE("pictures: the totems' visions and the films of the dead are described by their own features")
{
    CHECK(picture::OwnerOf(L"totem_guidance_2") == picture::FilmOwner::Totems);
    CHECK(picture::OwnerOf(L"totem_tragedy_4_ja") == picture::FilmOwner::Totems);
    CHECK(picture::OwnerOf(L"death_act1_1_sisters_cliff") == picture::FilmOwner::Credits);
    CHECK(picture::OwnerOf(L"death_act6_3_emily_eyegauge_3_ja") == picture::FilmOwner::Credits);
    // The events of the past play in the totems' menu, over a player of their own.
    CHECK(picture::OwnerOf(L"totem_events_of_the_past") == picture::FilmOwner::Films);
    CHECK(picture::OwnerOf(L"projectorghost_movie") == picture::FilmOwner::Films);
    CHECK(picture::OwnerOf(L"sam_end_credits") == picture::FilmOwner::Films);
}

TEST_CASE("pictures: a cut that differs only by Jessica's clothes or Mike's hand falls back to the film's own words")
{
    // The credits' scripts (act9_2 death_video_*.lua) queue these by JessWithJacket, JessNoJacket and
    // MikeLostFingers.
    CHECK(picture::FilmKeys(L"death_act8_5_jessica_leftbehind_jacket") ==
          Keys{L"film.death_act8_5_jessica_leftbehind_jacket", L"film.death_act8_5_jessica_leftbehind"});
    CHECK(picture::FilmKeys(L"death_act3_2_jessica_liftshaft_tshirt") ==
          Keys{L"film.death_act3_2_jessica_liftshaft_tshirt", L"film.death_act3_2_jessica_liftshaft"});
    CHECK(picture::FilmKeys(L"death_act7_1_emily_shotbymike_lostfingers") ==
          Keys{L"film.death_act7_1_emily_shotbymike_lostfingers", L"film.death_act7_1_emily_shotbymike"});
    // A cut's number last: Emily's three deaths at the lift are one film of three cuts unless each has
    // words of its own.
    CHECK(picture::FilmKeys(L"death_act6_3_emily_eyegauge_3") == Keys{L"film.death_act6_3_emily_eyegauge_3", L"film.death_act6_3_emily_eyegauge"});
    // A number glued to the word is part of the name.
    CHECK(picture::FilmKeys(L"death_act8_5_matt_timeout1") == Keys{L"film.death_act8_5_matt_timeout1"});
    CHECK(picture::FilmKeys(L"death_act8_5_jessica_keepstill_timeout2_jacket") ==
          Keys{L"film.death_act8_5_jessica_keepstill_timeout2_jacket", L"film.death_act8_5_jessica_keepstill_timeout2"});
    CHECK(picture::FilmKeys(L"Projector_Wendigo") == Keys{L"film.projector_wendigo"});
    CHECK(picture::FilmKeys(L"").empty());
}

TEST_CASE("pictures: a censored cut has words of its own where the table has them, else the film's")
{
    // The game plays the "_ja" cut with Extreme Content Censoring on (GetMovieFilePath): every key of
    // the film it censors with "_ja" first, so one censored text serves all the film's clothing cuts,
    // then the film's own keys, in that film's order.
    CHECK(picture::FilmKeys(L"death_act3_2_jessica_liftshaft_tshirt_ja") ==
          Keys{L"film.death_act3_2_jessica_liftshaft_tshirt_ja", L"film.death_act3_2_jessica_liftshaft_ja", L"film.death_act3_2_jessica_liftshaft_tshirt",
               L"film.death_act3_2_jessica_liftshaft"});
    CHECK(picture::FilmKeys(L"death_act6_3_emily_eyegauge_3_ja") ==
          Keys{L"film.death_act6_3_emily_eyegauge_3_ja", L"film.death_act6_3_emily_eyegauge_ja", L"film.death_act6_3_emily_eyegauge_3",
               L"film.death_act6_3_emily_eyegauge"});
    CHECK(picture::FilmKeys(L"death_act8_4_josh_wendigo_ja") == Keys{L"film.death_act8_4_josh_wendigo_ja", L"film.death_act8_4_josh_wendigo"});
    CHECK(picture::VisionKeys(L"totem_death_5_ja") == Keys{L"totem.totem_death_5_ja", L"totem.totem_death_5"});
    CHECK(picture::VisionKeys(L"totem_tragedy_4_JA") == Keys{L"totem.totem_tragedy_4_ja", L"totem.totem_tragedy_4"});
    // A name that is only the suffix is a name.
    CHECK(picture::VisionKeys(L"_ja") == Keys{L"totem._ja"});
    CHECK(picture::FilmKeys(L"_ja") == Keys{L"film._ja"});
}

TEST_CASE("pictures: the fifth hunger totem's three cuts fall back to the totem's words, the others are their own")
{
    CHECK(picture::VisionKeys(L"totem_hunger_5_2") == Keys{L"totem.totem_hunger_5_2", L"totem.totem_hunger_5"});
    CHECK(picture::VisionKeys(L"totem_death_1") == Keys{L"totem.totem_death_1"});
    CHECK(picture::VisionKeys(L"totem_events_of_the_past") == Keys{L"totem.totem_events_of_the_past"});
    CHECK(picture::VisionKeys(L"").empty());
}

TEST_CASE("pictures: a photo is known by its texture, a thing in the hand by its core name")
{
    CHECK(picture::PictureKey(L"T_camera_Sam_01_clr_UP") == L"picture.t_camera_sam_01_clr_up");
    CHECK(picture::LookKey(L"I_psychsession_1_Interaction") == L"look.i_psychsession_1_interaction");
    // Dr. Hill's card is described as it is turned to its face, whatever the case of its name.
    const auto card = picture::PlanFor(L"I_PsychSession_1_Interaction");
    REQUIRE(card.has_value());
    CHECK(card->moment == picture::Moment::Reveals);
    // Sam's sketchbook spread by spread, the station's map as it is held up (states of their anim
    // graphs, research/sessions/s58-whole.log, s54-full.log), the station's walls at once.
    const auto book = picture::PlanFor(L"I_sams_sketchbook");
    REQUIRE(book.has_value());
    CHECK(book->moment == picture::Moment::Pages);
    const auto map = picture::PlanFor(L"I_map");
    REQUIRE(map.has_value());
    CHECK(map->state == L"map_inspect1");
    const auto walls = picture::PlanFor(L"I_graffiti");
    REQUIRE(walls.has_value());
    CHECK(walls->moment == picture::Moment::Begins);
    // The chapter 1 graffiti is transcribed by the game itself (its reading widget).
    CHECK_FALSE(picture::PlanFor(L"I_carved_graffiti").has_value());
    CHECK_FALSE(picture::PlanFor(L"").has_value());
}

TEST_CASE("pictures: Sam's sketchbook says each spread as its graph comes to it, nothing while a page turns")
{
    CHECK(picture::PageKey(L"I_sams_sketchbook", 2) == L"look.i_sams_sketchbook.2");
    // The book's states as session 58 played them (research/sessions/s58-whole.log, 11:48:09 to
    // 11:49:35): opened on the second spread, on to the fourth, back to the first, put down.
    const std::vector<std::wstring> states = {
        L"start_to_reach1",  L"reach_idle1",      L"reach_to_closed_ik1",          L"closed_ik_idle",   L"closed_ik_topage2_right",
        L"page2_idle_right", L"turn_2nd_right_to_3rd_right", L"page3_idle_right", L"turn_3rd_right_to_4th_left", L"page4_idle_left1",
        L"turn_4th_left_to_3rd_left", L"page3_idle_left", L"turn_3rd_left_to_2nd_left", L"page2_idle_left", L"turn_2nd_left_to_1st_right",
        L"page1_idle_right", L"page1_idle_right_to_exit", L"Start_Pose"};
    std::vector<int> said;
    int last = 0;
    for (const auto& state : states)
    {
        const auto page = picture::PageOf(state);
        if (page && *page != last) said.push_back(last = *page);
    }
    CHECK(said == std::vector<int>{2, 3, 4, 3, 2, 1});
    // The idles of the template's other states, and what is no spread.
    CHECK(picture::PageOf(L"page1_idle_right2") == 1);
    CHECK(picture::PageOf(L"page4_idle_left2") == 4);
    CHECK_FALSE(picture::PageOf(L"page2_idle_left_to_exit_SEQ").has_value());
    CHECK_FALSE(picture::PageOf(L"page3_idle_right_to_exit").has_value());
    CHECK_FALSE(picture::PageOf(L"closed_ik_idle").has_value());
    CHECK_FALSE(picture::PageOf(L"page_idle").has_value());
    CHECK_FALSE(picture::PageOf(L"page0_idle").has_value());
    CHECK_FALSE(picture::PageOf(L"page2").has_value());
    CHECK_FALSE(picture::PageOf(L"").has_value());
    // Only the sketchbook is read so: the doll house's diary turns the same pages and is the game's
    // own words.
    CHECK_FALSE(picture::PlanFor(L"I_Doll_house_diary").has_value());
}

TEST_CASE("pictures: the camera's photo is described as the camera is raised and as each page turns, once in a row")
{
    picture::PhotoPager pager;
    // Not held yet: nothing, whatever the index.
    CHECK_FALSE(pager.Update(false, 0).has_value());
    // Raised on the first photo.
    CHECK(pager.Update(true, 0) == 0);
    CHECK_FALSE(pager.Update(true, 0).has_value());
    // The stick pages on, and back.
    CHECK(pager.Update(true, 1) == 1);
    CHECK_FALSE(pager.Update(true, 1).has_value());
    CHECK(pager.Update(true, 0) == 0);
    // Pushed past the last photo, the index stays (no wrap): nothing new.
    CHECK(pager.Update(true, 4) == 4);
    CHECK_FALSE(pager.Update(true, 4).has_value());
    // An index not known yet says nothing.
    CHECK_FALSE(pager.Update(true, -1).has_value());
    // Put down and raised again: the photo it shows is described again.
    CHECK_FALSE(pager.Update(false, 4).has_value());
    CHECK(pager.Update(true, 4) == 4);
    pager.Reset();
    CHECK(pager.Update(true, 4) == 4);
}

TEST_CASE("pictures: a long film is said in parts as it plays, the part it stands in once, none while static shows")
{
    // The events of the past, as the game plays them unlocked in part: a part shows, static, then the
    // film seeks on to the next unlocked part (UBatesEventsOfThePastMoviePlayer).
    const auto parts = picture::PartsOf(L"totem_events_of_the_past");
    REQUIRE(parts.size() == 7);
    CHECK(picture::PartKey(L"totem_events_of_the_past", 0) == L"film.totem_events_of_the_past.1");
    CHECK(picture::PartKey(L"Sam_End_Credits", 3) == L"film.sam_end_credits.4");
    CHECK(picture::PartsOf(L"projector_wendigo").empty());
    // The cinema's film is said in four parts, from its start.
    const auto sting = picture::PartsOf(L"smg_sting");
    REQUIRE(sting.size() == 4);
    CHECK(sting.front() == 0.0);
    // A film of the dead with a later part the credits show only in one ending (9 s, not 5 s).
    CHECK(picture::PartsOf(L"death_act1_1_sisters_cliff") == std::vector<double>{5.5});
    CHECK(picture::PartKey(L"death_act1_1_sisters_cliff", 0) == L"film.death_act1_1_sisters_cliff.1");
    CHECK(picture::PartsOf(L"death_act1_1_sisters_cliff_alone").empty());

    // The parts begin where the film shows them, measured on its own frames: the miners' photograph
    // at 20.5 s, the man with the flamethrower at 107.5 s.
    picture::PartTracker m;
    CHECK(m.Update(parts, 21.0, true) == size_t{1});
    CHECK(m.Update(parts, 108.0, true) == size_t{6});

    picture::PartTracker t;
    CHECK(t.Update(parts, 0.0, true) == size_t{0});
    CHECK_FALSE(t.Update(parts, 5.0, true).has_value());
    CHECK(t.Update(parts, 25.0, true) == size_t{1});
    // Static over a locked part: nothing, though the film's position has come to the third part, then
    // the seek lands in the fourth part, which is said; the third, skipped, is not.
    CHECK_FALSE(t.Update(parts, 26.0, false).has_value());
    CHECK_FALSE(t.Update(parts, 45.0, false).has_value());
    CHECK(t.Update(parts, 60.0, true) == size_t{3});
    CHECK_FALSE(t.Update(parts, 62.0, true).has_value());
    // The last part lasts to the film's end.
    CHECK(t.Update(parts, 110.0, true) == size_t{6});
    CHECK_FALSE(t.Update(parts, 124.0, true).has_value());
    // Played again from the start: said again, and again from the start while still in the first part.
    CHECK(t.Update(parts, 0.2, true) == size_t{0});
    CHECK_FALSE(t.Update(parts, 9.0, true).has_value());
    CHECK(t.Update(parts, 0.5, true) == size_t{0});
    // A film whose first part begins later says nothing before it (the epilogue's caption is the
    // game's own words).
    picture::PartTracker e;
    const auto epilogue = picture::PartsOf(L"sam_end_credits");
    CHECK_FALSE(e.Update(epilogue, 1.0, true).has_value());
    CHECK(e.Update(epilogue, 6.0, true) == size_t{0});
    e.Reset();
    CHECK(e.Update(epilogue, 6.0, true) == size_t{0});
}
