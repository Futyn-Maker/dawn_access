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

TEST_CASE("pictures: a photo is known by its texture, a thing in the hand by any name the game gives it")
{
    using Names = std::vector<std::wstring>;
    CHECK(picture::PictureKey(L"T_camera_Sam_01_clr_UP") == L"picture.t_camera_sam_01_clr_up");
    CHECK(picture::LookKey(L"I_psychsession_1_Interaction") == L"look.i_psychsession_1_interaction");
    // Dr. Hill's card has no core name and goes by its class, or by the name its activity starts it by
    // (research/sessions/s67-play.log, 19:16:06: BP_PsychSession_PickUp_C_1, never described); it is
    // described as it is turned to its face, under its one description, whatever the case.
    const auto card = picture::PlanFor(Names{L"", L"", L"BP_PsychSession_PickUp_C"});
    REQUIRE(card.has_value());
    CHECK(card->moment == picture::Moment::Reveals);
    CHECK(card->key == L"i_psychsession_1_interaction");
    const auto named = picture::PlanFor(Names{L"", L"I_PsychSession_1_Interaction", L"BP_PsychSession_PickUp_C"});
    REQUIRE(named.has_value());
    CHECK(named->key == L"i_psychsession_1_interaction");
    // Sam's sketchbook has none either: spread by spread, by its class or the level's name.
    const auto book = picture::PlanFor(Names{L"", L"", L"BP_sam_sketchbook_C"});
    REQUIRE(book.has_value());
    CHECK(book->moment == picture::Moment::Pages);
    CHECK(book->key == L"i_sams_sketchbook");
    CHECK(picture::PlanFor(Names{L"", L"I_sams_sketchbook", L""})->key == L"i_sams_sketchbook");
    // The station's map as it is held up, by its core name; the looks at it and the walls once the look
    // stands, the game's own idle states (research/sessions/s54-full.log, 22:11:56 to 22:21:14).
    const auto map = picture::PlanFor(Names{L"I_map", L"", L"BP_Map_C"});
    REQUIRE(map.has_value());
    CHECK(map->moment == picture::Moment::State);
    CHECK(map->state == L"map_inspect1");
    const auto walls = picture::PlanFor(Names{L"I_graffiti", L"", L"BP_Graffiti_C"});
    REQUIRE(walls.has_value());
    CHECK(walls->moment == picture::Moment::State);
    CHECK(walls->state == L"examine_idle");
    CHECK(picture::PlanFor(Names{L"I_lookatmap_with_emily", L"", L"BP_Lookatmap_with_emily_C"})->state == L"inspect_idle");
    CHECK(picture::PlanFor(Names{L"I_lookatmap_without_emily", L"", L"BP_Lookatmap_without_emily_C"})->state == L"inspect_idle1");
    // The security monitor in the lower ski-lift station, once Chris's look at it stands: its screen
    // cuts from a bathroom to a camera above the snow, a figure there and gone (s68, 01:28:07).
    const auto monitor = picture::PlanFor(Names{L"I_security_monitor", L"I_security_monitor", L"BP_Security_monitor_C"});
    REQUIRE(monitor.has_value());
    CHECK(monitor->moment == picture::Moment::State);
    CHECK(monitor->state == L"examine_idle");
    CHECK(monitor->key == L"i_security_monitor");
    CHECK(picture::PlanFor(Names{L"", L"", L"BP_Security_monitor_C"})->key == L"i_security_monitor");
    // The Washingtons' portrait in the lodge's garage vestibule, once Chris's look at it stands
    // (research/sessions/s70-play.log, 11:44:29): a painting, the same at every look.
    const auto portrait = picture::PlanFor(Names{L"I_family_portrait", L"I_family_portrait", L"BP_Family_portrait_C"});
    REQUIRE(portrait.has_value());
    CHECK(portrait->moment == picture::Moment::State);
    CHECK(portrait->state == L"examine_idle");
    CHECK(portrait->key == L"i_family_portrait");
    CHECK(picture::PlanFor(Names{L"", L"", L"BP_Family_portrait_C"})->key == L"i_family_portrait");
    // The things of session 71, by the names the logs give them: the pinboard once Sam lifts the
    // invitation (19:27:30), the dollhouse by its name alone (its class is also I_look_at_picture's), the
    // headless doll turned to its reveal (no core name, 20:22:43), the cable car desk once the look stands
    // (23:18:02).
    const auto board = picture::PlanFor(Names{L"I_pinboard_hannah_room", L"I_pinboard_hannah_room", L"BP_Pinboard_hannah_room_C"});
    REQUIRE(board.has_value());
    CHECK(board->moment == picture::Moment::State);
    CHECK(board->state == L"lift_paper_idle");
    CHECK(picture::PlanFor(Names{L"I_look_at_dollshouse", L"I_look_at_dollshouse", L"BP_Look_at_picture_C"})->state == L"examine_idle");
    CHECK_FALSE(picture::PlanFor(Names{L"I_look_at_picture", L"I_look_at_picture", L"BP_Look_at_picture_C"}).has_value());
    const auto doll = picture::PlanFor(Names{L"", L"I_Headlessdoll", L"BP_Headlessdoll_C"});
    REQUIRE(doll.has_value());
    CHECK(doll->moment == picture::Moment::Reveals);
    CHECK(doll->key == L"i_headlessdoll");
    CHECK(picture::PlanFor(Names{L"I_controls_lookat", L"I_controls_lookat", L"BP_controls_lookat_C"})->state == L"examine_idle");
    // The telescopes: Ashley's by its own core name; Mike's and Matt's, alike in every other name, by their
    // class's package (s71 15:21:08 and 20:25:18 logged both as I_Telescope, BP_Telescope_C).
    CHECK(picture::PlanFor(Names{L"A_telescope", L"A_telescope", L"BP_Telescope_C", L"/Game/Gameplay/Gameobjects/Activities/act2_1/Scenic_view/Telescope/BP_Telescope"})->key ==
          L"a_telescope");
    CHECK(picture::PlanFor(Names{L"I_Telescope", L"I_Telescope", L"BP_Telescope_C", L"/Game/Gameplay/Gameobjects/interactions/Act2_2/Telescope/BP_Telescope"})->key ==
          L"i_telescope.act2_2");
    CHECK(picture::PlanFor(Names{L"I_Telescope", L"I_Telescope", L"BP_Telescope_C", L"/Game/Gameplay/Gameobjects/interactions/act2_3/Telescope/BP_Telescope"})->key ==
          L"i_telescope.act2_3");
    CHECK_FALSE(picture::PlanFor(Names{L"I_Telescope", L"I_Telescope", L"BP_Telescope_C", L""}).has_value());
    // The silent things of chapters 1 and 2, as the logs of sessions 68 to 71 name them.
    CHECK(picture::PlanFor(Names{L"", L"I_Car_lookat", L"BP_Car_lookat_C"})->key == L"i_car_lookat");
    CHECK(picture::PlanFor(Names{L"I_MineMap", L"I_MineMap", L"BP_Minemap_C"})->state == L"look_idle1");
    CHECK(picture::PlanFor(Names{L"I_CigarStub", L"I_CigarStub", L"BP_Cigarstub_C"})->state == L"ik_animation_run");
    CHECK(picture::PlanFor(Names{L"I_childsdrawing", L"I_childsdrawing", L"BP_Childsdrawing_C"})->moment == picture::Moment::Reveals);
    CHECK(picture::PlanFor(Names{L"I_ReadNote", L"I_ReadNote", L"BP_Readnote_C"})->moment == picture::Moment::Reveals);
    CHECK(picture::PlanFor(Names{L"I_SnowyBench", L"I_SnowyBench", L"BP_Snowybench_C"})->state == L"BenchWipe2");
    CHECK(picture::PlanFor(Names{L"i_prom_night_photo_female", L"I_Prom_night_photo_female", L"BP_Prom_night_photo_female_C"})->key ==
          L"i_prom_night_photo_female");
    // The chapter 1 graffiti is transcribed by the game itself (its reading widget); a class shared by
    // other things (the generic wall examine the walls are made from) names no picture.
    CHECK_FALSE(picture::PlanFor(Names{L"I_carved_graffiti", L"", L"BP_Examine_wall_item_left_C"}).has_value());
    CHECK_FALSE(picture::PlanFor(Names{L"", L"", L""}).has_value());
    CHECK_FALSE(picture::PlanFor(Names{}).has_value());
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
    CHECK_FALSE(picture::PlanFor(std::vector<std::wstring>{L"I_Doll_house_diary", L"", L"BP_Doll_house_diary_C"}).has_value());
}

TEST_CASE("pictures: the camera's photo is described as the camera is raised and as each page turns, once in a row")
{
    // A prompt said long before: the first photo waits its while (kPromptWaitMost) for the camera's own.
    picture::PhotoPager pager;
    // Not held yet: nothing, whatever the index.
    CHECK_FALSE(pager.Update(false, 0, 0.0, -1.0).has_value());
    // Raised on the first photo, its prompt said 0.3 s on.
    CHECK_FALSE(pager.Update(true, 0, 1.0, -1.0).has_value());
    CHECK_FALSE(pager.Update(true, 0, 1.3, 1.3).has_value());
    CHECK(pager.Update(true, 0, 1.6, 1.3) == 0);
    CHECK_FALSE(pager.Update(true, 0, 1.7, 1.3).has_value());
    // The stick pages on, and back, at once.
    CHECK(pager.Update(true, 1, 2.0, 1.3) == 1);
    CHECK_FALSE(pager.Update(true, 1, 2.1, 1.3).has_value());
    CHECK(pager.Update(true, 0, 2.2, 1.3) == 0);
    // Pushed past the last photo, the index stays (no wrap): nothing new.
    CHECK(pager.Update(true, 4, 2.5, 1.3) == 4);
    CHECK_FALSE(pager.Update(true, 4, 2.6, 1.3).has_value());
    // An index not known yet says nothing.
    CHECK_FALSE(pager.Update(true, -1, 2.7, 1.3).has_value());
    // Put down and raised again: the photo it shows is described again, after its prompt again.
    CHECK_FALSE(pager.Update(false, 4, 3.0, 1.3).has_value());
    CHECK_FALSE(pager.Update(true, 4, 4.0, 1.3).has_value());
    CHECK(pager.Update(true, 4, 4.5, 4.2) == 4);
    // No prompt at all: the photo once its while has run.
    pager.Reset();
    CHECK_FALSE(pager.Update(true, 4, 10.0, -1.0).has_value());
    CHECK_FALSE(pager.Update(true, 4, 11.9, -1.0).has_value());
    CHECK(pager.Update(true, 4, 12.0, -1.0) == 4);
}

TEST_CASE("pictures: the prologue's camera says its photo after the game's prompt, 2026-11-10")
{
    // research/sessions/s67-play.log: the camera raised at 18:44:30.767 (its photo 1), "A, D" and the way
    // back said at 18:44:31.528, the pages turned at 39.068, 40.969, 42.771 and 44.175 (photos 2 to 5).
    // The photo had come before the prompt, and its words were never found (CameraPictures read off
    // its place).
    picture::PhotoPager pager;
    const double held = 30.767;
    const double prompt = 31.528;
    std::vector<std::pair<double, int>> said;
    const auto look = [&](double now, int index, double promptAt)
    {
        if (const auto shown = pager.Update(true, index, now, promptAt)) said.emplace_back(now, *shown);
    };
    for (double now = held; now < 33.0; now += 0.1)
        look(now, 0, now >= prompt ? prompt : -1.0);
    look(39.068, 1, prompt);
    look(40.969, 2, prompt);
    look(42.771, 3, prompt);
    look(44.175, 4, prompt);
    REQUIRE(said.size() == 5);
    CHECK(said[0].second == 0);
    CHECK(said[0].first >= prompt + picture::kPromptSettle);
    CHECK(said[0].first < prompt + picture::kPromptSettle + 0.11);
    CHECK(said[4] == std::pair<double, int>{44.175, 4});
}

TEST_CASE("pictures: a picture's words come after the game's prompt and once the player's key is let go: Dr. Hill's card, 2026-11-11")
{
    // research/sessions/s68-play.log, 01:03:36: the card came face up at .254 as D still turned it, its
    // words said at once and cut by the reader at the key's repeats; "Назад: Backspace" came on the bar
    // at .452.
    const double shown = 36.254;
    const double prompt = 36.452;
    // The key let go before the prompt has settled: the words come kPromptSettle after the prompt.
    {
        picture::Due due;
        due.Set(shown, true);
        std::optional<double> said;
        for (double now = shown; now < 38.0 && !said; now += 1.0 / 30.0)
        {
            if (due.Ready(now, now >= prompt ? prompt : 10.0, now < 36.40)) said = now;
        }
        REQUIRE(said.has_value());
        CHECK(*said >= prompt + picture::kPromptSettle);
        CHECK(*said < prompt + picture::kPromptSettle + 0.04);
        CHECK_FALSE(due.Pending());
        CHECK_FALSE(due.Ready(38.0, prompt, false));
    }
    // The key held on past the prompt: the words wait for it to be let go.
    {
        picture::Due due;
        due.Set(shown, true);
        CHECK_FALSE(due.Ready(37.0, prompt, true));
        CHECK(due.WaitsForKey(37.0, true));
        CHECK(due.Ready(37.1, prompt, false));
    }
    // Held on and on: said once kKeyWaitMost has gone.
    {
        picture::Due due;
        due.Set(shown, true);
        CHECK_FALSE(due.Ready(shown + picture::kKeyWaitMost - 0.01, prompt, true));
        CHECK(due.Ready(shown + picture::kKeyWaitMost, prompt, true));
    }
    // No prompt with it (the map's were said as it was taken down, before): kLookPromptWaitMost.
    {
        picture::Due due;
        due.Set(shown, true);
        const double earlier = shown - 4.0;
        CHECK_FALSE(due.Ready(shown + picture::kLookPromptWaitMost - 0.01, earlier, false));
        CHECK(due.Ready(shown + picture::kLookPromptWaitMost, earlier, false));
    }
    // A spread turned to waits for no prompt, the key alone.
    {
        picture::Due due;
        due.Set(shown, false);
        CHECK_FALSE(due.Ready(shown, -1.0, true));
        CHECK(due.Ready(shown + 0.1, -1.0, false));
    }
    // Nothing set, nothing due.
    picture::Due idle;
    CHECK_FALSE(idle.Pending());
    CHECK_FALSE(idle.Ready(1.0, 0.5, false));
}

TEST_CASE("pictures: the camera's photo waits for the key that paged to it to be let go")
{
    picture::PhotoPager pager;
    // Raised, its prompt said and settled: the first photo, the key up.
    CHECK_FALSE(pager.Update(true, 0, 1.0, -1.0, false).has_value());
    CHECK(pager.Update(true, 0, 1.6, 1.3, false) == 0);
    // D down, the page turned: the photo waits while D is held.
    CHECK_FALSE(pager.Update(true, 1, 2.0, 1.3, true).has_value());
    CHECK_FALSE(pager.Update(true, 2, 2.3, 1.3, true).has_value());
    // Let go on the third photo: that one, the second never.
    CHECK(pager.Update(true, 2, 2.4, 1.3, false) == 2);
    CHECK_FALSE(pager.Update(true, 2, 2.5, 1.3, false).has_value());
    // A key held on and on: the photo once kKeyWaitMost has gone from its page.
    CHECK_FALSE(pager.Update(true, 3, 3.0, 1.3, true).has_value());
    CHECK_FALSE(pager.Update(true, 3, 3.0 + picture::kKeyWaitMost - 0.01, 1.3, true).has_value());
    CHECK(pager.Update(true, 3, 3.0 + picture::kKeyWaitMost, 1.3, true) == 3);
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

TEST_CASE("pictures: the events of the past are described on the key, the part the film stands in alone: 2026-11-10")
{
    // Andrey: the film's narration never stops, so its parts' words, said as it played, ran over it
    // (research/sessions/s66-play.log, 15:56:53 to 15:58:40); a key says the part it stands in.
    CHECK(picture::OnKey(L"totem_events_of_the_past"));
    CHECK(picture::OnKey(L"Totem_Events_Of_The_Past"));
    // The other long films have no narration over their parts and go on being said as they play.
    CHECK_FALSE(picture::OnKey(L"smg_sting"));
    CHECK_FALSE(picture::OnKey(L"sam_end_credits"));
    const auto parts = picture::PartsOf(L"totem_events_of_the_past");
    REQUIRE(parts.size() == 7);
    // A press says the part the film is in then, not the ones before it.
    CHECK(picture::PartAt(parts, 0.0, true) == std::optional<size_t>(0));
    CHECK(picture::PartAt(parts, 25.0, true) == std::optional<size_t>(1));
    CHECK(picture::PartAt(parts, 51.0, true) == std::optional<size_t>(3));
    CHECK(picture::PartAt(parts, 124.0, true) == std::optional<size_t>(6));
    // Over a part not unlocked the film shows static: no part.
    CHECK_FALSE(picture::PartAt(parts, 25.0, false));
    // The epilogue's film begins its first part 5.5 s in: nothing before it.
    CHECK_FALSE(picture::PartAt(picture::PartsOf(L"sam_end_credits"), 2.0, true));
}

TEST_CASE("films: played again where it stood, a film goes on; played anew, it begins again")
{
    // s71: the cinema's projector stopped with the game's pause at 9.4 s and went on at 9.4 s.
    CHECK(picture::Resumes(L"smg_sting", L"smg_sting", 9.4, 9.4));
    CHECK(picture::Resumes(L"smg_sting", L"smg_sting", 9.4, 15.0));
    // Played from its start again, or another film: begun anew.
    CHECK_FALSE(picture::Resumes(L"smg_sting", L"smg_sting", 69.9, 0.1));
    CHECK_FALSE(picture::Resumes(L"smg_sting", L"another", 9.4, 9.4));
    // Never played before: no place to go on from.
    CHECK_FALSE(picture::Resumes(L"", L"smg_sting", -1.0, 0.1));
    CHECK_FALSE(picture::Resumes(L"smg_sting", L"smg_sting", 0.2, 0.3));
}

TEST_CASE("films: each part of a showing is said once, whichever player shows it")
{
    // s71 23:39:44: the cinema's screen and its projector began "smg_sting" together.
    picture::Showing showing;
    showing.Playing(100.0);
    CHECK(showing.Says(0));
    CHECK_FALSE(showing.Says(0));
    // The projector back from the pause in the first part still: nothing again; the next part once.
    CHECK(showing.Says(1));
    CHECK_FALSE(showing.Says(1));
    // The whole film's words count apart from its parts.
    CHECK(showing.Says(-1));
    // Played on: the showing lasts; no player for half a minute: over.
    showing.Playing(160.0);
    CHECK_FALSE(showing.Over(185.0));
    CHECK(showing.Over(191.0));
}

TEST_CASE("pictures: a film in the world is described only while its screen is drawn")
{
    // s71 23:12:38: act4_3's CCTV film begins as the act's opening shot shows the Stranger's monitor:
    // said at once.
    picture::Sight cctv;
    cctv.Begin(true);
    CHECK(cctv.Update(picture::Screen::Drawn));
    CHECK(cctv.Seen());
    CHECK_FALSE(cctv.Update(picture::Screen::Drawn)); // once
    // 23:19:59: Matt at the cable car station's map, the monitor far off and not drawn: the read-screen
    // key leaves it out.
    CHECK_FALSE(cctv.Update(picture::Screen::Hidden));
    CHECK_FALSE(cctv.Seen());
    // A film that begins on a screen out of view: its words wait, and come once it is drawn.
    picture::Sight later;
    later.Begin(true);
    CHECK_FALSE(later.Update(picture::Screen::Hidden));
    CHECK(later.Waiting());
    CHECK_FALSE(later.Update(picture::Screen::Hidden));
    CHECK(later.Update(picture::Screen::Drawn));
    CHECK_FALSE(later.Waiting());
    CHECK_FALSE(later.Update(picture::Screen::Drawn));
    // A film whose screen the mod finds none of is seen while it plays, as before.
    picture::Sight unknown;
    unknown.Begin(true);
    CHECK(unknown.Update(picture::Screen::Unknown));
    CHECK(unknown.Seen());
    // A film said in parts waits for nothing as it begins; its parts go by what is seen.
    picture::Sight parts;
    parts.Begin(false);
    CHECK_FALSE(parts.Update(picture::Screen::Drawn));
    CHECK(parts.Seen());
}

TEST_CASE("pictures: a film's screen gone with its level leaves it unseen until a screen is found again")
{
    // No screen ever found for it (a full-screen film): seen while it plays, as before.
    CHECK(picture::ScreenOf(false, 0, false) == picture::Screen::Unknown);
    // Its screen drawn, or not.
    CHECK(picture::ScreenOf(true, 1, true) == picture::Screen::Drawn);
    CHECK(picture::ScreenOf(true, 2, false) == picture::Screen::Hidden);
    // Its screens all gone with a level the game let go: not seen, though once found; found again as the
    // level streams back, drawn or not as the engine says.
    CHECK(picture::ScreenOf(true, 0, false) == picture::Screen::Hidden);
}
