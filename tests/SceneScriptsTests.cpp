// features/SceneScripts: what the scenes' scripts wait for when the character walks, read from
// excerpts of the game's own exported scripts, and which of those waits the game is asking now.
#include "Support.hpp"
#include "features/SceneScripts.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace da;
using scripts::Poll;
using scripts::Waits;

namespace
{
    // act1_3's scene03_shootingrange.lua: after the bag at the station, the scene waits for Sam
    // in the range's volume, then starts the range.
    const std::wstring kRange = LR"lua(
function Scene03_shootingrange_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 340)
  if SF_IsInTrigger(sm, {gameObjects = "Sam",
      trigger = "T_a_shootingrange"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,7)
    return
  end
end

function Scene03_shootingrange_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 466)
  if SF_IsActivityRunning(sm, {activity = "A_ShootingRange"}) then
    SF_SendSoundEvent(sm, {value = "Set_State_as_02_gun_range_intro"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,2)
    return
  end
end

function Scene03_shootingrange_State_7(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 2476)
  SF_StartActivity(sm, {activities = "A_range_letsshoot",
      delayUntilNextFrame = true})
  SF_TurnOffCurrentObjective(sm, {})
end

gRegisteredStatemachines.Scene03_shootingrange = {
    startState = 1,
    name = 'Scene03_shootingrange',
    states = {
      {nextState = nil,	func = Scene03_shootingrange_State_3, 	funcList = 'Is Sam In Trigger T_a_shootingrange'},
      {nextState = nil,	func = Scene03_shootingrange_State_4, 	funcList = 'Is Activity A_ShootingRange Running?'},
      {nextState = 4,	func = Scene03_shootingrange_State_7, 	funcList = 'Start Activity A_range_letsshoot, Turn Off Current Objective'},
    },
  } -- Done registering the SM
)lua";

    // act1_3's dialogue_control.lua: idle talk at the station; the state it passes on to waits
    // for the bag's choice to be over, which walking in does not bring about.
    const std::wstring kStationTalk = LR"lua(
function Dialogue_control_State_6(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 2656)
  if SF_IsActivityComplete(sm, {activity = "A_arrival_privacy_choice"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,11)
    return
  end
end
function Dialogue_control_State_10(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 2762)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_idleconversation_nearstation"}) then
    SF_IdleDialogue(sm, {idleSpeechGroup = "SamShoutsHello",
        mode = "Remove"})
    SF_ScriptedDialogue(sm, {sentenceGroup = "act1_3_arrival_samnearbag",
        mode = "Essential"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,6)
    return
  end
end
function Dialogue_control_State_21(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8906)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_crow_station_takeoffup"}) then
    SF_IdleDialogue(sm, {idleSpeechGroup = "SamShoutsHello",
        mode = "Add"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,10)
    return
  end
end
gRegisteredStatemachines.Dialogue_control = {
    states = {
      {nextState = nil,	func = Dialogue_control_State_6, 	funcList = 'Is Activity A_arrival_privacy_choice Complete?'},
      {nextState = nil,	func = Dialogue_control_State_10, 	funcList = 'Is Player In Trigger T_idleconversation_nearstation'},
      {nextState = nil,	func = Dialogue_control_State_21, 	funcList = 'Is Player In Trigger T_crow_station_takeoffup'},
    },
  }
)lua";

    // act2_1's scene02_lovetriangle.lua: Mike's scare is set off in one volume, and only then is
    // the next one, which starts his choice, asked about.
    const std::wstring kLoveTriangle = LR"lua(
function Scene02_lovetriangle_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 163)
  SF_StartActivity(sm, {activities = "A_lovetriangle_mike_choice"})
  SF_SendSoundEvent(sm, {value = "Set_State_as_02_wood_bridge_enter"})
end
function Scene02_lovetriangle_State_7(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4640)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_lovetriangle_mike_choice"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,3)
    return
  end
end
function Scene02_lovetriangle_State_10(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4740)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_LoveTriangle_MikeScare_Intro"}) then
    SF_ScriptedDialogue(sm, {sentenceGroup = "act2_1_lovetriangle_mikescare_intro",
        mode = "Essential"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,7)
    return
  end
end
gRegisteredStatemachines.Scene02_lovetriangle = {
    states = {
      {nextState = 5,	func = Scene02_lovetriangle_State_3, 	funcList = 'Start Activity A_lovetriangle_mike_choice, Send Sound Event Set_State_as_02_wood_bridge_enter '},
      {nextState = nil,	func = Scene02_lovetriangle_State_7, 	funcList = 'Is Player In Trigger T_lovetriangle_mike_choice'},
      {nextState = nil,	func = Scene02_lovetriangle_State_10, 	funcList = 'Is Player In Trigger T_LoveTriangle_MikeScare_Intro'},
    },
  }
)lua";

    // act2_4's dialogue_control.lua and scene01_coldwater.lua: both machines ask about the
    // fireplace's volume, one for a line of dialogue, one to start Josh lighting the fire, which
    // it asks about only after the distant scream upstairs.
    const std::wstring kFireplaceTalk = LR"lua(
function Dialogue_control_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 3313)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_bedrooms_hannahsroom"}) then
    gVariables.TriggeredHannahsRoom = 1
    SF_ScriptedDialogue(sm, {sentenceGroup = "act2_4_coldwater_hannahsroom",
        mode = "Essential"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,10)
    return
  end
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_joshlightingfire"}) then
    SF_ScriptedDialogue(sm, {sentenceGroup = "act2_4_coldwater_lightingfire1",
        mode = "Essential"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,5)
    return
  end
end
function Dialogue_control_State_10(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8859)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_joshlightingfire"}) then
    SF_ScriptedDialogue(sm, {sentenceGroup = "act2_4_coldwater_lightingfire1",
        mode = "Essential"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,5)
    return
  end
end
gRegisteredStatemachines.Dialogue_control = {
    states = {
      {nextState = nil,	func = Dialogue_control_State_4, 	funcList = 'Is Player In Trigger T_bedrooms_hannahsroom, Is Player In Trigger T_joshlightingfire'},
      {nextState = nil,	func = Dialogue_control_State_10, 	funcList = 'Is Player In Trigger T_joshlightingfire'},
    },
  }
)lua";

    const std::wstring kColdWater = LR"lua(
function Scene01_coldwater_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 977)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_distantscream"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,4)
    return
  end
end
function Scene01_coldwater_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 979)
  SF_StartActivity(sm, {activities = "A_distantscream"})
end
function Scene01_coldwater_State_5(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 981)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_joshlightingfire"}) then
    SF_Sleep(sm, {gameObjects = "Chris"})
    SF_StartActivity(sm, {activities = "A_joshlightingfire"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,9)
    return
  end
end
gRegisteredStatemachines.Scene01_coldwater = {
    states = {
      {nextState = nil,	func = Scene01_coldwater_State_3, 	funcList = 'Is Player In Trigger T_distantscream'},
      {nextState = 5,	func = Scene01_coldwater_State_4, 	funcList = 'Start Activity A_distantscream'},
      {nextState = nil,	func = Scene01_coldwater_State_5, 	funcList = 'Is Player In Trigger T_joshlightingfire'},
    },
  }
)lua";

    const Poll* Find(const std::vector<Poll>& polls, const std::wstring& trigger, int state = -1)
    {
        for (const auto& p : polls)
        {
            if (p.trigger == trigger && (state < 0 || p.state == state)) return &p;
        }
        return nullptr;
    }

    bool Has(const std::vector<std::wstring>& list, const std::wstring& one)
    {
        return std::find(list.begin(), list.end(), one) != list.end();
    }

    std::vector<Poll> Both(const std::wstring& a, const std::wstring& sourceA, const std::wstring& b, const std::wstring& sourceB)
    {
        auto polls = scripts::ParsePolls(a, sourceA);
        auto more = scripts::ParsePolls(b, sourceB);
        polls.insert(polls.end(), more.begin(), more.end());
        return polls;
    }
}

TEST_CASE("scripts: chapter one's range waits for Sam in its volume, 2026-11-01")
{
    // 17:24:22 the objective "Follow Chris and find out what he found"; the scene's machine
    // waited for Sam in T_a_shootingrange, which the list never offered (the trigger has no
    // script and no action of its own: "nothing happens there"), and the player was left with
    // nothing to walk to.
    const auto polls = scripts::ParsePolls(kRange, L"scene03_shootingrange");
    REQUIRE(polls.size() == 1);
    const Poll& range = polls.front();
    CHECK(range.machine == L"Scene03_shootingrange");
    CHECK(range.state == 3);
    CHECK(range.trigger == L"T_a_shootingrange");
    CHECK(range.who == L"Sam");
    CHECK_FALSE(range.negated);
    CHECK(range.story);
    CHECK(Has(range.calls, L"SF_StartActivity"));
    CHECK(Has(range.calls, L"SF_TurnOffCurrentObjective"));
    CHECK(Has(range.activities, L"A_range_letsshoot"));
    // What waits after the range has started is not on the way on, nor what its wait does once
    // the range runs.
    CHECK_FALSE(Has(range.activities, L"A_ShootingRange"));
    CHECK_FALSE(Has(range.calls, L"SF_SendSoundEvent"));

    Waits waits;
    waits.SetPolls(polls);
    // Before the scene asks, it does not wait on the volume.
    CHECK(waits.Judge(10.0, L"T_a_shootingrange", L"Sam").kind == Waits::Kind::NotNow);
    waits.Asked(10.0, L"T_a_shootingrange", true);
    const auto now = waits.Judge(10.3, L"T_a_shootingrange", L"Sam");
    CHECK(now.kind == Waits::Kind::Waiting);
    REQUIRE(now.poll);
    CHECK(now.poll->state == 3);
    // Asked about Sam: nothing to Chris.
    CHECK(waits.Judge(10.3, L"T_a_shootingrange", L"Chris").kind == Waits::Kind::None);
    // Asked about someone else than the player's character: no wait of hers.
    Waits other;
    other.SetPolls(polls);
    other.Asked(10.0, L"T_a_shootingrange", false);
    CHECK(other.Judge(10.3, L"T_a_shootingrange", L"Sam").kind == Waits::Kind::NotNow);
    // Once she has walked in, the machine moves on and stops asking.
    CHECK(waits.Judge(10.0 + scripts::kAskedWithin + 0.1, L"T_a_shootingrange", L"Sam").kind == Waits::Kind::NotNow);
}

TEST_CASE("scripts: the station's idle talk moves nothing on")
{
    const auto polls = scripts::ParsePolls(kStationTalk, L"dialogue_control");
    const Poll* near = Find(polls, L"T_idleconversation_nearstation");
    const Poll* crow = Find(polls, L"T_crow_station_takeoffup");
    REQUIRE(near);
    REQUIRE(crow);
    // The state passed on to waits for the bag's choice: no call of the way on starts it.
    CHECK_FALSE(near->story);
    CHECK_FALSE(Has(near->activities, L"A_arrival_privacy_choice"));
    // The crows' volume leads to the talk's question, which moves nothing on either.
    CHECK_FALSE(crow->story);
    Waits waits;
    waits.SetPolls(polls);
    waits.Asked(5.0, L"T_idleconversation_nearstation", true);
    CHECK(waits.Judge(5.1, L"T_idleconversation_nearstation", L"Sam").kind == Waits::Kind::NoStory);
    CHECK(waits.Judge(5.1, L"T_a_shootingrange", L"Sam").kind == Waits::Kind::None);
}

TEST_CASE("scripts: a volume that leads only to the next question moves the scene on as that one does")
{
    const auto polls = scripts::ParsePolls(kLoveTriangle, L"scene02_lovetriangle");
    const Poll* intro = Find(polls, L"T_LoveTriangle_MikeScare_Intro");
    const Poll* choice = Find(polls, L"T_lovetriangle_mike_choice");
    REQUIRE(intro);
    REQUIRE(choice);
    CHECK(choice->story);
    // Its own way on is a line of dialogue, but the choice's volume is asked about only after it.
    CHECK(intro->story);
    CHECK(Has(intro->calls, L"SF_ScriptedDialogue"));
}

TEST_CASE("scripts: two machines ask about the fireplace; one placed elsewhere is not the one asking")
{
    const auto polls = Both(kFireplaceTalk, L"dialogue_control", kColdWater, L"scene01_coldwater");
    const Poll* talk = Find(polls, L"T_joshlightingfire", 4);
    const Poll* fire = Find(polls, L"T_joshlightingfire", 5);
    REQUIRE(talk);
    REQUIRE(fire);
    CHECK(talk->machine == L"Dialogue_control");
    CHECK_FALSE(talk->story);
    CHECK(fire->machine == L"Scene01_coldwater");
    CHECK(fire->story);
    CHECK(Find(polls, L"T_distantscream")->story);

    Waits waits;
    waits.SetPolls(polls);
    // The act's start: the talk asks about Hannah's room and the fireplace, the scene about the
    // scream upstairs, which only its state 3 asks about: the scene stands there, so the
    // fireplace is asked about by the talk alone.
    for (double t = 0.0; t < 3.0; t += 0.04)
    {
        waits.Asked(t, L"T_bedrooms_hannahsroom", true);
        waits.Asked(t, L"T_joshlightingfire", true);
        waits.Asked(t, L"T_distantscream", true);
    }
    CHECK(waits.Judge(3.0, L"T_joshlightingfire", L"Sam").kind == Waits::Kind::NotNow);
    CHECK(waits.Judge(3.0, L"T_distantscream", L"Sam").kind == Waits::Kind::Waiting);
    // After the scream the scene asks about the fireplace, and nothing places it elsewhere.
    for (double t = 3.0; t < 6.0; t += 0.04)
    {
        waits.Asked(t, L"T_bedrooms_hannahsroom", true);
        waits.Asked(t, L"T_joshlightingfire", true);
    }
    const auto later = waits.Judge(6.0, L"T_joshlightingfire", L"Sam");
    CHECK(later.kind == Waits::Kind::Waiting);
    REQUIRE(later.poll);
    CHECK(later.poll->machine == L"Scene01_coldwater");
    CHECK(waits.Judge(6.0, L"T_distantscream", L"Sam").kind == Waits::Kind::NotNow);
}

TEST_CASE("scripts: a question answered by leaving waits for her to go, not to come")
{
    const std::wstring script = LR"lua(
function Scene05_intothelodge_State_32(sm)
  Debug_SetSMEDStateID(sm, 1)
  if not SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_Cam_LodgeUpper_20"}) then
    SF_StartActivity(sm, {activities = "A_something"})
    M_JumpToState(sm,30)
    return
  end
end
gRegisteredStatemachines.Scene05_intothelodge = {
    states = {
      {nextState = nil,	func = Scene05_intothelodge_State_32, 	funcList = 'NOT Is Player In Trigger T_Cam_LodgeUpper_20'},
    },
  }
)lua";
    const auto polls = scripts::ParsePolls(script, L"scene05");
    REQUIRE(polls.size() == 1);
    CHECK(polls.front().negated);
    CHECK_FALSE(polls.front().story);
}

TEST_CASE("scripts: a wait for time on the way on passes by itself")
{
    const std::wstring script = LR"lua(
function M_State_1(sm)
  if SF_IsInTrigger(sm, {gameObjects = "Player", trigger = "T_a"}) then
    M_JumpToState(sm,2)
    return
  end
end
function M_State_2(sm)
  if SF_Wait(sm, {seconds = 0.5,
      uniqueID = 1}) then
    M_JumpToState(sm,3)
    return
  end
end
function M_State_3(sm)
  SF_StartActivity(sm, {activities = "A_next"})
end
gRegisteredStatemachines.M = {
    states = {
      {nextState = nil,	func = M_State_1, 	funcList = 'Is Player In Trigger T_a'},
      {nextState = nil,	func = M_State_2, 	funcList = 'Wait 0.5 Seconds'},
      {nextState = nil,	func = M_State_3, 	funcList = 'Start Activity A_next'},
    },
  }
)lua";
    const auto polls = scripts::ParsePolls(script, L"m");
    REQUIRE(polls.size() == 1);
    CHECK(polls.front().story);
    CHECK(Has(polls.front().activities, L"A_next"));
}

namespace
{
    // Act2_2's aicontroller.lua: Jessica's loop, a branch state registered to pass on to a wait of
    // a tenth of a second that comes back to it, with its questions nested in other conditions.
    const std::wstring kJessicaLoop = LR"lua(
function Aicontroller_State_6(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 3116)
  if SF_Wait(sm, {seconds = 0.1,
      uniqueID = 1}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,7)
    return
  end
end
function Aicontroller_State_7(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 3143)
  if SF_IsActivityComplete(sm, {activity = "I_Generator"}) then
    M_JumpToState(sm,35)
    return
  end
  if SF_Comparison(sm, {number2 = 0,
      number1 = gVariables.SelfieDone,
      type = "Number",
      operation = "Equal"}) then
    if SF_IsInTrigger(sm, {gameObjects = "Player",
        trigger = "T_selfie_leave"}) then
      if SF_Comparison(sm, {string2 = "WaitingForSelfie",
          string1 = gVariables.JessicasOrder,
          type = "String"}) then
        M_JumpToState(sm,29)
        return
      end
    end
  end
  if SF_Comparison(sm, {number2 = 0,
      number1 = gVariables.ConversationDone,
      type = "Number",
      operation = "Equal"}) then
    if SF_IsActivityComplete(sm, {activity = "A_emilybash_conversation"}) then
      M_JumpToState(sm,27)
      return
    end
    if SF_IsInTrigger(sm, {gameObjects = "Player",
        trigger = "T_conversation_leave"}) then
      if not SF_Comparison(sm, {string2 = "Escort",
          string1 = gVariables.JessicasOrder,
          type = "String"}) then
        M_JumpToState(sm,34)
        return
      end
    end
  end
end
function Aicontroller_State_11(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 3183)
  if SF_IsActivityComplete(sm, {activity = "A_EnterMineChoice"}) then
    M_JumpToState(sm,39)
    return
  end
end
function Aicontroller_State_16(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 3349)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_MineEntrance"}) then
    M_JumpToState(sm,17)
    return
  end
end
function Aicontroller_State_17(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 3351)
  SF_OrderLeadTo(sm, {lead = "Player",
      character = "Jessica",
      marker = "AIMarker_Mine_Edge",
      moveType = "Marker",
      movementStyle = "Lead_5m"})
end
function Aicontroller_State_34(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4004)
  SF_OrderEscort(sm, {separationStyle = "Separate_5m",
      character = "Jessica",
      escort = "Player",
      escortStyle = "Prefer_Left"})
  gVariables.JessicasOrder = "Escort"
  gVariables.ConversationDone = 1
  SF_SetEnableActivity(sm, {activities = "A_whyfight_conversation"})
  SF_SetEnableActivity(sm, {activities = "A_emilybash_conversation"})
end
function Aicontroller_State_44(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4200)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_JessicaWait_TotemFortune"}) then
    M_JumpToState(sm,64)
    return
  end
end
function Aicontroller_State_45(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4203)
  if SF_Wait(sm, {seconds = 0.1,
      uniqueID = 2}) then
    M_JumpToState(sm,44)
    return
  end
end
function Aicontroller_State_63(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6377)
  SF_OrderEscort(sm, {stickCloseToEscort = true,
      character = "Jessica",
      escort = "Player",
      escortStyle = "Prefer_Right"})
  gVariables.JessicasOrder = "WaitingOutsideTotem"
end
function Aicontroller_State_64(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6381)
  if not SF_Comparison(sm, {string2 = "WaitingOutsideTotem",
      string1 = gVariables.JessicasOrder,
      type = "String"}) then
    M_JumpToState(sm,63)
    return
  end
end
gRegisteredStatemachines.Aicontroller = {
    states = {
      {nextState = nil,	func = Aicontroller_State_6, 	funcList = 'Wait 0.1 Seconds'},
      {nextState = 6,	func = Aicontroller_State_7, 	funcList = 'Is Activity I_Generator Complete?, Is *SelfieDone = 0, Is *ConversationDone = 0'},
      {nextState = nil,	func = Aicontroller_State_11, 	funcList = 'Is Activity A_EnterMineChoice Complete?'},
      {nextState = nil,	func = Aicontroller_State_16, 	funcList = 'Is Player In Trigger T_MineEntrance'},
      {nextState = 11,	func = Aicontroller_State_17, 	funcList = 'Order Jessica to Lead Player to AIMarker_Mine_Edge using Lead_5m'},
      {nextState = 6,	func = Aicontroller_State_34, 	funcList = 'Order Jessica To Escort Player To M_Generator_Jessica Using Separate_5m'},
      {nextState = 45,	func = Aicontroller_State_44, 	funcList = 'Is Player In Trigger T_JessicaWait_TotemFortune'},
      {nextState = nil,	func = Aicontroller_State_45, 	funcList = 'Wait 0.1 Seconds'},
      {nextState = 45,	func = Aicontroller_State_63, 	funcList = 'Order Jessica To Escort Player To AIMarker_JessicaWait_TotemFortune Using Separate_5m'},
      {nextState = 45,	func = Aicontroller_State_64, 	funcList = 'NOT Is *JessicasOrder = WaitingOutsideTotem'},
    },
  }
)lua";

    // act2_11's I_answerphone-Custom.lua: one look in passing on its way to enabling the
    // answerphone again.
    const std::wstring kAnswerphone = LR"lua(
function Event_I_answerphone_Custom_State_3(sm)
  Debug_SetSMEDStateID(sm, 4662)
  SF_End(sm, {statemachine = nil})
  return
end
function Event_I_answerphone_Custom_State_6(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4671)
  SF_SetEnableActivity(sm, {activities = "I_answerphone",
      enable = true})
end
function Event_I_answerphone_Custom_State_8(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4679)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_IntoLodge_NearAnswermachine"}) then
    SF_ScriptedDialogue(sm, {sentenceGroup = "generics_chris_2_1_answermachine",
        mode = "Essential"})
    M_JumpToState(sm,6)
    return
  end
end
gRegisteredStatemachines.Event_I_answerphone_Custom = {
    states = {
      {nextState = nil,	func = Event_I_answerphone_Custom_State_3, 	funcList = 'End '},
      {nextState = 3,	func = Event_I_answerphone_Custom_State_6, 	funcList = 'Enable Activity I_answerphone'},
      {nextState = 6,	func = Event_I_answerphone_Custom_State_8, 	funcList = 'Is Player In Trigger T_IntoLodge_NearAnswermachine'},
    },
  }
)lua";

    // act2_11's scene05_intothelodge_nosey_josh.lua: Chris, the player there, walking past sends
    // Josh on, and the state stays where it is.
    const std::wstring kNoseyJosh = LR"lua(
function Scene05_intothelodge_nosey_josh_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4545)
  if SF_IsInTrigger(sm, {gameObjects = "Chris",
      trigger = "T_chris_josh_walkpast_noseyjosh"}) then
    if SF_FirstTime(sm, {uniqueID = 1}) then
      SF_OrderLeadTo(sm, {lead = "Player",
          character = "Josh",
          marker = "M_noseyjosh",
          moveType = "Marker",
          movementStyle = "Lead_3m"})
    end
  end
  if SF_IsActivityComplete(sm, {activity = "A_nosey_josh"}) then
    SF_OrderForceIdle(sm, {character = "Josh"})
    M_JumpToState(sm,3)
    return
  end
end
gRegisteredStatemachines.Scene05_intothelodge_nosey_josh = {
    states = {
      {nextState = nil,	func = Scene05_intothelodge_nosey_josh_State_2, 	funcList = 'Is Chris In Trigger T_chris_josh_walkpast_noseyjosh, Is Activity A_nosey_josh Complete?'},
    },
  }
)lua";
}

TEST_CASE("scripts: questions nested in other conditions are read, and a loop through a wait for time waits")
{
    // The review of 2026-11-01: 49 of the game's 462 questions stand inside another condition.
    const auto polls = scripts::ParsePolls(kJessicaLoop, L"aicontroller");
    const Poll* leave = Find(polls, L"T_conversation_leave");
    const Poll* selfie = Find(polls, L"T_selfie_leave");
    REQUIRE(leave);
    REQUIRE(selfie);
    CHECK(leave->state == 7);
    CHECK(selfie->state == 7);
    // State 7 passes on to the wait of 6, which comes back to it: it asks at every turn.
    CHECK(leave->waits);
    CHECK(leave->leaves);
    CHECK(Has(leave->calls, L"SF_OrderEscort"));
    // Leaving the talk only turns the conversations off and has Jessica escort the player on to
    // the generator, which is listed for itself: nothing of a scene starts there.
    CHECK(Has(leave->calls, L"SF_SetEnableActivity"));
    CHECK(leave->activities.empty());
    CHECK_FALSE(leave->story);
    // What the state's other conditions ask is not on the way on.
    CHECK_FALSE(Has(leave->activities, L"I_Generator"));
    CHECK_FALSE(Has(selfie->activities, L"I_Generator"));
}

TEST_CASE("scripts: a companion sent on ahead moves the scene on, one escorting the player does not")
{
    const auto polls = scripts::ParsePolls(kJessicaLoop, L"aicontroller");
    // At the mine's entrance Jessica is led on to its edge, where the mine's choice begins.
    const Poll* mine = Find(polls, L"T_MineEntrance");
    REQUIRE(mine);
    CHECK(Has(mine->calls, L"SF_OrderLeadTo"));
    CHECK(mine->story);
    // At the totem she is made to wait while the player looks: an escort, the player leads.
    const Poll* totem = Find(polls, L"T_JessicaWait_TotemFortune");
    REQUIRE(totem);
    CHECK(totem->waits);
    CHECK_FALSE(totem->story);
}

TEST_CASE("scripts: a look in passing and a yes that moves nothing are no waits")
{
    // The answerphone's state passes on to enable it again whatever the answer: asked once per
    // use, it was a way on for a second after each one.
    const auto phone = scripts::ParsePolls(kAnswerphone, L"I_answerphone-Custom");
    REQUIRE(phone.size() == 1);
    CHECK_FALSE(phone.front().waits);
    CHECK_FALSE(phone.front().story);
    // Chris walking past sends Josh on once and the state stays, asking for good.
    const auto josh = scripts::ParsePolls(kNoseyJosh, L"scene05_intothelodge_nosey_josh");
    const Poll* walkpast = Find(josh, L"T_chris_josh_walkpast_noseyjosh");
    REQUIRE(walkpast);
    CHECK(walkpast->waits);
    CHECK_FALSE(walkpast->leaves);
    CHECK_FALSE(walkpast->story);
}

TEST_CASE("scripts: a trigger's script")
{
    SUBCASE("a companion ordered on does not move the scene on: it runs every time (act2_3, T_EmilyWait_GateToBridge)")
    {
        const std::wstring script = LR"lua(
gEvents["EnterTrigger"]["T_EmilyWait_GateToBridge"] = {
	stateMachine = "Event_t_emilywait_gatetobridge_enter",
	triggerLimit = -1,
	delay = 0,
}
function Event_T_EmilyWait_GateToBridge_Enter_State_1(sm)
  Debug_SetSMEDStateID(sm, 2)
  SF_Start(sm, {})
end
function Event_T_EmilyWait_GateToBridge_Enter_State_2(sm)
  Debug_SetSMEDStateID(sm, 5)
  SF_OrderLeadTo(sm, {lead = "Player",
      character = "Emily",
      movementStyle = "Lead_15m"})
end
gRegisteredStatemachines.Event_t_emilywait_gatetobridge_enter = {
    startState = 1,
    name = 'Event_t_emilywait_gatetobridge_enter',
    states = {
      {nextState = 2,	func = Event_T_EmilyWait_GateToBridge_Enter_State_1, 	funcList = 'Start'},
      {nextState = 3,	func = Event_T_EmilyWait_GateToBridge_Enter_State_2, 	funcList = 'Order Emily to Lead Player to M_EmilyWait_GateToBridge using Lead_15m'},
    },
  } -- Done registering the SM
)lua";
        scripts::EnterEvent event;
        REQUIRE(scripts::ParseEnterEvent(script, event));
        CHECK(event.trigger == L"T_EmilyWait_GateToBridge");
        CHECK(event.limit == -1);
        CHECK(Has(event.calls, L"SF_OrderLeadTo"));
        CHECK_FALSE(Has(event.calls, L"SF_Start"));
        // Counted, it stayed a way on for the whole level, before and after she walked through
        // (the review of 2026-11-01: five volumes of act2_11 and act2_3).
        CHECK_FALSE(event.story);
    }
    SUBCASE("an ambient animation alone does not, an activity of the story does")
    {
        scripts::EnterEvent pao;
        REQUIRE(scripts::ParseEnterEvent(LR"lua(gEvents["EnterTrigger"]["T_crow"] = { triggerLimit = 1, }
  SF_StartActivity(sm, {activities = "PAO_crow_takeoff"}))lua",
                                         pao));
        CHECK(pao.limit == 1);
        CHECK_FALSE(pao.story);
        scripts::EnterEvent poster;
        REQUIRE(scripts::ParseEnterEvent(LR"lua(gEvents["EnterTrigger"]["T_wanted_poster"] = { triggerLimit = -1, }
  SF_StartActivity(sm, {activities = "I_wanted_poster_into"}))lua",
                                         poster));
        CHECK(poster.story);
        // Turning an activity on only makes it available, and what it makes available is listed.
        scripts::EnterEvent enable;
        REQUIRE(scripts::ParseEnterEvent(LR"lua(gEvents["EnterTrigger"]["T_x"] = { triggerLimit = 1, }
  SF_SetEnableActivity(sm, {activities = "I_x", enable = true}))lua",
                                         enable));
        CHECK_FALSE(enable.story);
    }
    SUBCASE("a script for another event is none")
    {
        scripts::EnterEvent none;
        CHECK_FALSE(scripts::ParseEnterEvent(L"gEvents[\"LeaveTrigger\"][\"T_x\"] = {}", none));
    }
}

TEST_CASE("scripts: the totem plaque's trigger only turns its look on again, 2026-11-01")
{
    // act1_3's T_totem_plaque_lookat-Enter.lua, as the game ships it: the look on the plaque is
    // turned on again if the totem's lesson was not seen; the state that started it is cut off (its
    // designers' comments say so). Listed as a way on behind her, it was walked to, and nothing
    // happened there.
    const std::wstring script = LR"lua(
gEvents["EnterTrigger"]["T_totem_plaque_lookat"] = {
	stateMachine = "Event_t_totem_plaque_lookat_enter",
	triggerLimit = 1,
	delay = 0,
}
function Event_T_totem_plaque_lookat_Enter_State_1(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 2)
  SF_Start(sm, {})
end
function Event_T_totem_plaque_lookat_Enter_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5)
  SF_SetEnableActivity(sm, {activities = "I_totem_plaque_lookat",
      enable = true})
end
function Event_T_totem_plaque_lookat_Enter_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8)
  if not SF_IsCallforwardEqualTo(sm, {callforward = "TotemTutorialSeen",
      dataType = "Bool",
      type = "General"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,2)
    return
  end
end
function Event_T_totem_plaque_lookat_Enter_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 11)
  SF_End(sm, {statemachine = nil})
  return
end
function Event_T_totem_plaque_lookat_Enter_State_5(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 14)
  SF_StartActivity(sm, {activities = "I_totem_plaque_lookat"})
end
gRegisteredStatemachines.Event_t_totem_plaque_lookat_enter = {
    startState = 1,
    name = 'Event_t_totem_plaque_lookat_enter',
    states = {
      {nextState = 3,	func = Event_T_totem_plaque_lookat_Enter_State_1, 	funcList = 'Start'},
      {nextState = 4,	func = Event_T_totem_plaque_lookat_Enter_State_2, 	funcList = 'Enable Activity I_totem_plaque_lookat'},
      {nextState = 4,	func = Event_T_totem_plaque_lookat_Enter_State_3, 	funcList = 'NOT Is Callforward TotemTutorialSeen True'},
      {nextState = nil,	func = Event_T_totem_plaque_lookat_Enter_State_4, 	funcList = 'End '},
      {nextState = nil,	func = Event_T_totem_plaque_lookat_Enter_State_5, 	funcList = 'Start Activity I_totem_plaque_lookat'},
    },
  } -- Done registering the SM
)lua";
    scripts::EnterEvent event;
    REQUIRE(scripts::ParseEnterEvent(script, event));
    CHECK(event.trigger == L"T_totem_plaque_lookat");
    CHECK(event.limit == 1);
    CHECK(Has(event.calls, L"SF_SetEnableActivity"));
    // The start the designers cut off is not reached.
    CHECK_FALSE(Has(event.calls, L"SF_StartActivity"));
    CHECK(event.activities.empty());
    CHECK_FALSE(event.story);
}

TEST_CASE("scripts: a trigger script run once stays run when the player changes character, 2026-11-01")
{
    // Chapter one: Sam walked into the totem plaque's trigger, a script run once; at the range
    // Chris took over, and the record kept by character listed the trigger again behind him.
    scripts::EnterEvent plaque;
    plaque.trigger = L"T_totem_plaque_lookat";
    plaque.limit = 1;
    scripts::EnterEvent poster;
    poster.trigger = L"T_wanted_poster";
    poster.limit = -1;
    scripts::SetOff record;
    CHECK_FALSE(record.Spent(plaque));
    record.Walked(L"T_totem_plaque_lookat");
    record.Walked(L"T_wanted_poster");
    // Sam, then Chris: the game's count is the script's.
    CHECK(record.Spent(plaque));
    // One that runs every time is never spent.
    CHECK_FALSE(record.Spent(poster));
    // The next level's scripts start their counts afresh.
    record.NewScripts();
    CHECK_FALSE(record.Spent(plaque));
}

TEST_CASE("scripts: a trigger script the game has used up is spent though no look saw her walk in: the roof's fall, 2026-11-08")
{
    // act6_1's T_hannahslair_roofcollapse (run once) starts A_hannahslair_intoremains as Emily walks
    // in: at 16:00:44 the walk to it ended in the scene, her look had never seen her inside, and
    // after the scene (16:01:04) the trigger was listed again 15 m back; she walked to it, reached
    // it, and nothing happened, the game's script having been taken out of gEvents.EnterTrigger.
    const auto script = std::wstring(LR"lua(
if gEvents["EnterTrigger"] == nil then
	gEvents["EnterTrigger"] = {}
end
gEvents["EnterTrigger"]["T_hannahslair_roofcollapse"] = {
	stateMachine = "Event_t_hannahslair_roofcollapse_enter",
	triggerLimit = 1,
	delay = 0,
}
function Event_T_hannahslair_roofcollapse_Enter_State_1(sm)
  Debug_SetSMEDStateID(sm, 3928)
  SF_Start(sm, {})
end
function Event_T_hannahslair_roofcollapse_Enter_State_2(sm)
  Debug_SetSMEDStateID(sm, 3930)
  if SF_IsActivityComplete(sm, {activity = "A_hannahslair_roofcollapse"}) then
    M_JumpToState(sm,4)
    return
  end
end
function Event_T_hannahslair_roofcollapse_Enter_State_3(sm)
  Debug_SetSMEDStateID(sm, 3933)
  SF_StartActivity(sm, {activities = "A_hannahslair_intoremains"})
end
function Event_T_hannahslair_roofcollapse_Enter_State_4(sm)
  Debug_SetSMEDStateID(sm, 3934)
  SF_End(sm, {statemachine = nil})
  return
end
gRegisteredStatemachines.Event_t_hannahslair_roofcollapse_enter = {
    startState = 1,
    name = 'Event_t_hannahslair_roofcollapse_enter',
    states = {
      {nextState = 3,	func = Event_T_hannahslair_roofcollapse_Enter_State_1, 	funcList = 'Start'},
      {nextState = nil,	func = Event_T_hannahslair_roofcollapse_Enter_State_2, 	funcList = 'Is Activity A_hannahslair_roofcollapse Complete?'},
      {nextState = 2,	func = Event_T_hannahslair_roofcollapse_Enter_State_3, 	funcList = 'Start Activity A_hannahslair_intoremains'},
      {nextState = nil,	func = Event_T_hannahslair_roofcollapse_Enter_State_4, 	funcList = 'End '},
    },
  }
)lua");
    scripts::EnterEvent roof;
    REQUIRE(scripts::ParseEnterEvent(script, roof));
    CHECK(roof.trigger == L"T_hannahslair_roofcollapse");
    CHECK(roof.limit == 1);
    CHECK(roof.story);
    using Entry = scripts::SetOff::Entry;
    scripts::SetOff record;
    // Before she walks in, the game keeps the script: a way on.
    CHECK_FALSE(record.Spent(roof, Entry::Kept));
    // The scene played; no look of the mod saw her inside. The game's record says spent.
    CHECK_FALSE(record.Spent(roof));
    CHECK(record.Spent(roof, Entry::Gone));
    // The game's record stands over the mod's either way: kept is not spent whatever the mod saw
    // (a trigger run twice of three, or a look that took her for inside a moment early).
    record.Walked(L"T_hannahslair_roofcollapse");
    CHECK(record.Spent(roof));
    CHECK_FALSE(record.Spent(roof, Entry::Kept));
    // Where the scripts cannot be read, the mod's own record answers.
    CHECK(record.Spent(roof, Entry::Unknown));
    record.NewScripts();
    CHECK_FALSE(record.Spent(roof, Entry::Unknown));
    // A read that fails a moment (the scripts' lock held by another thread) keeps the game's record
    // as last read: the spent trigger does not turn back into a way on for that look.
    CHECK(record.Known(L"T_hannahslair_roofcollapse", Entry::Gone) == Entry::Gone);
    CHECK(record.Known(L"T_hannahslair_roofcollapse", Entry::Unknown) == Entry::Gone);
    CHECK(record.Spent(roof, record.Known(L"T_hannahslair_roofcollapse", Entry::Unknown)));
    CHECK(record.Known(L"T_other", Entry::Unknown) == Entry::Unknown);
    // The next level's scripts start their own record.
    record.NewScripts();
    CHECK(record.Known(L"T_hannahslair_roofcollapse", Entry::Unknown) == Entry::Unknown);
}

namespace
{
    // act2_1's MainFlow sets the wait every scene of the act makes between its shots.
    const wchar_t* const kMainFlowVariables = LR"lua(
function Mainflow_State_16(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 2875)
  gVariables.ScreenDisplayCount = 0
  gVariables.JessWasHitCount = 0
  gVariables.MikeWasHitCount = 0
  gVariables.SceneTransitionFadeWaitTime = 2
end
)lua";

    // act2_1's scene04_snowballfight.lua: Jessica is made the player, the machine waits, then
    // starts her scene.
    const wchar_t* const kSnowballSwitch = LR"lua(
function Scene04_snowballfight_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 3099)
  SF_SetPlayerCharacter(sm, {character = "Jessica"})
  SF_Sleep(sm, {sleep = true,
      gameObjects = "Emily"})
  SF_Sleep(sm, {sleep = true,
      gameObjects = "Ashley"})
  SF_Sleep(sm, {sleep = true,
      gameObjects = "Matt"})
end

function Scene04_snowballfight_State_9(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4793)
  if SF_Wait(sm, {seconds = gVariables.SceneTransitionFadeWaitTime,
      uniqueID = 1}) then
    SF_StartActivity(sm, {activities = "A_snowballfight_intro_choices"})
    SF_SendSoundEvent(sm, {value = "SetState_Ambience_SkiLiftExterior_SnowBallFight"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,8)
    return
  end
end

gRegisteredStatemachines.Scene04_snowballfight = {
    startState = 1,
    name = 'Scene04_snowballfight',
    states = {
      {nextState = 9,	func = Scene04_snowballfight_State_4, 	funcList = 'Set Jessica To Be The Player, Put Emily  to sleep., Put Ashley  to sleep., Put Matt  to sleep.'},
      {nextState = nil,	func = Scene04_snowballfight_State_9, 	funcList = 'Wait *SceneTransitionFadeWaitTime Seconds'},
    },
}
)lua";

    // scene03_scenicview.lua: Ashley is made the player, a state lights the view and passes on,
    // then the wait.
    const wchar_t* const kScenicSwitch = LR"lua(
function Scene03_scenicview_State_5(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4784)
  SF_SetPlayerCharacter(sm, {character = "Ashley"})
  SF_SetLightVisible(sm, {visibility = "Show",
      lightGroupName = "Telescopeview_Switch_On",
      mode = "LightGroup"})
end

function Scene03_scenicview_State_7(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8533)
  SF_SetInjuryMask(sm, {injuryName = "SnowOnBoots",
      mesh = "Body",
      character = "Ashley"})
  SF_SetShaderRamp(sm, {totaltime = 1.0,
      gameObjectName = "Ashley__Ashley_outdoor",
      name = "SnowFade",
      endvalue = 1.0,
      startvalue = 0.0})
end

function Scene03_scenicview_State_6(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4786)
  if SF_Wait(sm, {seconds = gVariables.SceneTransitionFadeWaitTime,
      uniqueID = 1}) then
    SF_StartActivity(sm, {activities = "A_scenicview_sceneintro"})
    M_JumpToState(sm,4)
    return
  end
end

gRegisteredStatemachines.Scene03_scenicview = {
    startState = 1,
    name = 'Scene03_scenicview',
    states = {
      {nextState = 7,	func = Scene03_scenicview_State_5, 	funcList = 'Set Ashley To Be The Player, Show Light Group Telescopeview_Switch_On'},
      {nextState = nil,	func = Scene03_scenicview_State_6, 	funcList = 'Wait *SceneTransitionFadeWaitTime Seconds'},
      {nextState = 6,	func = Scene03_scenicview_State_7, 	funcList = 'Set injury mask on Ashley to SnowOnBoots., Set shader ramp'},
    },
}
)lua";

    // Act2_2's MainFlow: Mike made the player, then the act's machines run; nothing waits.
    const wchar_t* const kPlainSwitch = LR"lua(
function Mainflow_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 27)
  SF_SetPlayerCharacter(sm, {character = "Mike"})
end

function Mainflow_State_5(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 476)
  SF_RunStatemachine(sm, {statemachine = "Mainflow_nodesetups"})
  SF_RunStatemachine(sm, {statemachine = "Dialogue_control"})
  SF_RunStatemachine(sm, {statemachine = "Aicontroller"})
  SF_RunStatemachine(sm, {statemachine = "Animsetcontroller"})
end

gRegisteredStatemachines.Mainflow = {
    startState = 1,
    name = 'Mainflow',
    states = {
      {nextState = 5,	func = Mainflow_State_4, 	funcList = 'Set Mike To Be The Player'},
      {nextState = 6,	func = Mainflow_State_5, 	funcList = 'Run Statemachine Mainflow_nodesetups, Run Statemachine Dialogue_control, Run Statemachine Aicontroller, Run Statemachine Animsetcontroller'},
    },
}
)lua";
}

TEST_CASE("scripts: a scene that switches the player's character and waits before it goes on, 2026-11-02")
{
    const auto variables = scripts::ParseVariables(kMainFlowVariables);
    REQUIRE(variables.count(L"SceneTransitionFadeWaitTime") == 1);
    CHECK(variables.at(L"SceneTransitionFadeWaitTime") == doctest::Approx(2.0));
    CHECK(variables.at(L"JessWasHitCount") == doctest::Approx(0.0));
    const auto jessica = scripts::ParseSwitches(kSnowballSwitch, variables);
    REQUIRE(jessica.size() == 1);
    CHECK(jessica[0].character == L"Jessica");
    CHECK(jessica[0].state == 4);
    CHECK(jessica[0].seconds == doctest::Approx(2.0));
    // Past a state that only acts and passes on.
    const auto ashley = scripts::ParseSwitches(kScenicSwitch, variables);
    REQUIRE(ashley.size() == 1);
    CHECK(ashley[0].character == L"Ashley");
    CHECK(ashley[0].seconds == doctest::Approx(2.0));
    // A switch the scene does not wait after.
    const auto mike = scripts::ParseSwitches(kPlainSwitch, variables);
    REQUIRE(mike.size() == 1);
    CHECK(mike[0].character == L"Mike");
    CHECK(mike[0].seconds == 0.0);
    // The wait unknown without the act's variables: no wait is taken.
    CHECK(scripts::ParseSwitches(kSnowballSwitch, {})[0].seconds == 0.0);
}

TEST_CASE("scripts: a volume the scenes waited on stays a way on while its machine leaves off asking, 2026-11-05")
{
    // Jessica's lead out of the mine (research/sessions/s44.log): Aicontroller 44's SF_OrderLeadTo
    // asked about T_ExitMine at the hand-over after the cart (20:42:57), left off at 20:45:06 as Mike
    // went to the totem, asked again at 20:47:51, and so on four times in fourteen minutes, each time
    // round an interaction of his; the way out came and went from the list with it.
    using Kind = Waits::Kind;
    scripts::HeldWaits held;
    CHECK(held.Waiting(L"T_ExitMine", Kind::Waiting, false)); // 20:42:57
    CHECK(held.Waiting(L"T_ExitMine", Kind::NotNow, false));  // 20:45:06: left off, still a way on
    CHECK(held.Waiting(L"T_ExitMine", Kind::NotNow, false));  // 20:46:13, after the totem
    CHECK(held.Waiting(L"T_ExitMine", Kind::Waiting, false)); // 20:47:51
    // A volume never waited on is no way on for a state that is not the one asking.
    CHECK_FALSE(held.Waiting(L"T_BeamFall", Kind::NotNow, false));
    // Walked into while it is not asked about: she is there, it is held no longer.
    CHECK_FALSE(held.Waiting(L"T_ExitMine", Kind::NotNow, true));
    CHECK_FALSE(held.Waiting(L"T_ExitMine", Kind::NotNow, false));
    // Asked about again, it is a way on again.
    CHECK(held.Waiting(L"T_ExitMine", Kind::Waiting, false));
    // The story moved on, or the objective changed: what was waited on before is forgotten.
    held.Forget();
    CHECK_FALSE(held.Waiting(L"T_ExitMine", Kind::NotNow, false));
    // A volume no state of a scene moves on for any more is no way on, whatever it was.
    CHECK(held.Waiting(L"T_Runaway_cart", Kind::Waiting, false));
    CHECK_FALSE(held.Waiting(L"T_Runaway_cart", Kind::NoStory, false));
    CHECK_FALSE(held.Waiting(L"T_Runaway_cart", Kind::NotNow, false));
}

TEST_CASE("scripts: the lounge's trigger starts the lost-phone talk only while its variable allows it, 2026-11-06")
{
    // act2_5's T_EnteredLounge-Enter.lua as the game ships it. The talk is armed when the cold and dark
    // scene ends (bAllowLostPhone = 1) and the script sets it to 0 as it starts the talk; it runs every
    // time she walks in. In the cabin on 2026-11-06 (research/sessions/s46-full.log) Mike walked into it
    // on his way to the matches at 21:00:48 and the talk ran; the volume stood listed as a way on for the
    // next twenty minutes, said reached, walked into again and again, and never gone, and at the shutters
    // it was the way on 4 m ahead that nothing came of.
    const std::wstring script = LR"lua(
if gEvents["EnterTrigger"] == nil then
	gEvents["EnterTrigger"] = {}
end
gEvents["EnterTrigger"]["T_EnteredLounge"] = {
	stateMachine = "Event_t_enteredlounge_enter",
	triggerLimit = -1,
	delay = 0,
}
function Event_T_EnteredLounge_Enter_State_1(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5139)
  SF_Start(sm, {})
end

function Event_T_EnteredLounge_Enter_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5141)
  SF_StartActivity(sm, {activities = "A_lostphone"})
  gVariables.bAllowLostPhone = 0
end

function Event_T_EnteredLounge_Enter_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5145)
  --If there was anything after the following End-command in the statemachine, that
  --was dropped since it's not allowed in lua to have anything after a return
  SF_End(sm, {statemachine = nil})
  return
end

function Event_T_EnteredLounge_Enter_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5195)
  if SF_Comparison(sm, {number2 = 1,
      number1 = gVariables.bAllowLostPhone,
      type = "Number",
      operation = "Equal"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,2)
    return
  end
end


-- Register the statemachine with the SM handler
gRegisteredStatemachines.Event_t_enteredlounge_enter = {
    startState = 1,
    name = 'Event_t_enteredlounge_enter',
    states = {
      {nextState = 4,	func = Event_T_EnteredLounge_Enter_State_1, 	funcList = 'Start'},
      {nextState = 3,	func = Event_T_EnteredLounge_Enter_State_2, 	funcList = 'Start Activity A_lostphone, *bAllowLostPhone = 0'},
      {nextState = nil,	func = Event_T_EnteredLounge_Enter_State_3, 	funcList = 'End '},
      {nextState = 3,	func = Event_T_EnteredLounge_Enter_State_4, 	funcList = 'Is *bAllowLostPhone = 1'},
    },
  } -- Done registering the SM----
)lua";
    scripts::EnterEvent event;
    REQUIRE(scripts::ParseEnterEvent(script, event));
    CHECK(event.trigger == L"T_EnteredLounge");
    CHECK(event.limit == -1);
    // Every condition taken either way, the talk is what it can do.
    CHECK(event.story);
    CHECK(Has(event.activities, L"A_lostphone"));
    REQUIRE(event.variables.size() == 1);
    CHECK(event.variables.front() == L"bAllowLostPhone");
    using Kind = scripts::Number::Kind;
    const auto with = [](scripts::Number value) { return [value](const std::wstring& name) { return name == L"bAllowLostPhone" ? value : scripts::Number{}; }; };
    // Armed: walking in starts the talk.
    CHECK(scripts::StoryNow(event, with({Kind::Value, 1.0})));
    // After the talk, and before it is armed (nothing set yet): nothing of the story happens there.
    CHECK_FALSE(scripts::StoryNow(event, with({Kind::Value, 0.0})));
    CHECK_FALSE(scripts::StoryNow(event, with({Kind::Nil, 0.0})));
    // The game's state not read: either way, as before.
    CHECK(scripts::StoryNow(event, with({})));
}

TEST_CASE("scripts: a comparison is weighed as the scripts' library weighs it")
{
    using Kind = scripts::Number::Kind;
    const scripts::Number one{Kind::Value, 1.0}, zero{Kind::Value, 0.0}, nil{Kind::Nil, 0.0}, unknown{};
    CHECK(scripts::Compare(L"Equal", one, one) == true);
    CHECK(scripts::Compare(L"Equal", zero, one) == false);
    CHECK(scripts::Compare(L"NotEqual", zero, one) == true);
    CHECK(scripts::Compare(L"GreaterThanOrEqual", one, zero) == true);
    CHECK(scripts::Compare(L"LessThan", one, zero) == false);
    // Misc.lua's nil cases: a nil equals only a nil and orders nothing.
    CHECK(scripts::Compare(L"Equal", nil, one) == false);
    CHECK(scripts::Compare(L"Equal", nil, nil) == true);
    CHECK(scripts::Compare(L"LessThanOrEqual", nil, nil) == true);
    CHECK(scripts::Compare(L"NotEqual", nil, one) == true);
    CHECK(scripts::Compare(L"LessThan", nil, one) == false);
    CHECK(scripts::Compare(L"NotEqual", one, nil) == true);
    CHECK(scripts::Compare(L"GreaterThan", one, nil) == false);
    CHECK_FALSE(scripts::Compare(L"Equal", unknown, one).has_value());
    CHECK_FALSE(scripts::Compare(L"Add", one, one).has_value());

    const auto vars = [](const std::wstring& name) { return name == L"VisitsNOTE" ? scripts::Number{scripts::Number::Kind::Value, 2.0} : scripts::Number{}; };
    // act1_3's graffiti line: number1 a variable, number2 a number.
    CHECK(scripts::Holds(LR"(SF_Comparison(sm, {number2 = 1, number1 = gVariables.VisitsNOTE, type = "Number", operation = "LessThanOrEqual"}))", vars) ==
          false);
    CHECK(scripts::Holds(LR"(not SF_Comparison(sm, {number2 = 1, number1 = gVariables.VisitsNOTE, type = "Number", operation = "LessThanOrEqual"}))", vars) ==
          true);
    // Anything else is no answer: a string comparison, a variable not read, another call, two joined.
    CHECK_FALSE(scripts::Holds(LR"(SF_Comparison(sm, {string2 = "inspect", string1 = sm.variables.name, type = "String"}))", vars).has_value());
    CHECK_FALSE(scripts::Holds(LR"(SF_Comparison(sm, {number2 = 0, number1 = gVariables.Other, type = "Number", operation = "Equal"}))", vars).has_value());
    CHECK_FALSE(scripts::Holds(LR"(SF_IsActivityComplete(sm, {activity = "A_x"}))", vars).has_value());
    CHECK_FALSE(scripts::Holds(LR"(SF_Comparison(sm, {number2 = 1, number1 = gVariables.VisitsNOTE, type = "Number", operation = "Equal"}) and SF_Wait(sm, {seconds = 1}))",
                               vars)
                    .has_value());
}

TEST_CASE("scripts: a comparison that holds and jumps takes the machine past its next state")
{
    // A state that asks once and passes on: when the comparison holds it jumps, and the state its
    // registered next one names is never run (M_JumpToState puts the jump in place of it).
    const std::wstring script = LR"lua(
gEvents["EnterTrigger"]["T_x"] = {
	stateMachine = "Event_t_x_enter",
	triggerLimit = -1,
	delay = 0,
}
function Event_T_x_Enter_State_1(sm)
  if SF_Comparison(sm, {number2 = 1,
      number1 = gVariables.Done,
      type = "Number",
      operation = "Equal"}) then
    M_JumpToState(sm,3)
    return
  end
end
function Event_T_x_Enter_State_2(sm)
  SF_StartActivity(sm, {activities = "A_x"})
end
function Event_T_x_Enter_State_3(sm)
  SF_End(sm, {statemachine = nil})
  return
end
gRegisteredStatemachines.Event_t_x_enter = {
    startState = 1,
    name = 'Event_t_x_enter',
    states = {
      {nextState = 2,	func = Event_T_x_Enter_State_1, 	funcList = 'Is *Done = 1'},
      {nextState = 3,	func = Event_T_x_Enter_State_2, 	funcList = 'Start Activity A_x'},
      {nextState = nil,	func = Event_T_x_Enter_State_3, 	funcList = 'End '},
    },
  }
)lua";
    scripts::EnterEvent event;
    REQUIRE(scripts::ParseEnterEvent(script, event));
    CHECK(event.story);
    using Kind = scripts::Number::Kind;
    const auto done = [](double v) { return [v](const std::wstring&) { return scripts::Number{Kind::Value, v}; }; };
    // Done: past the activity, nothing happens.
    CHECK_FALSE(scripts::StoryNow(event, done(1.0)));
    // Not done: on to the activity.
    CHECK(scripts::StoryNow(event, done(0.0)));
}

TEST_CASE("scripts: a question whose way on always comes back to it moves nothing on: the library's security camera, 2026-11-06")
{
    // act3_11's scene02_spookylibrary_cctv.lua as the game ships it. Walking into T_Cam_Library_50 plays
    // the camera's look (a tarantula with it, once per visit, only when Hill's spider was chosen), the
    // machine waits for her to leave, turns the look off and asks again. In the library on 2026-11-06
    // (research/sessions/s47-full.log) the volume stood a way on after "Find Josh", said reached and
    // never gone.
    const std::wstring script = LR"lua(-- State Machine Export
-- Please do not edit this file directly but use SMED2 instead.
-- This was exported from \dev_ps4\ud\assets\work_files\levels\game\act3_11\scripts\scene02_spookylibrary_cctv.sm2

-------------------------------------
-- Here there be state's functions --
-------------------------------------

function Scene02_spookylibrary_cctv_State_1(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6066)
  SF_Start(sm, {})
end

function Scene02_spookylibrary_cctv_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6068)
  SF_SendSoundEvent(sm, {value = "Postprocess_CCTV_On"})
  SF_StartScreenEffect(sm, {resourceName = "CCTV_Effect"})
  SF_SuppressCloseUpCamera(sm, {suppress = true})
  SF_DebugText(sm, {text = "Hello, world!",
      duration = 5.0})
end

function Scene02_spookylibrary_cctv_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6073)
  SF_SendSoundEvent(sm, {value = "Postprocess_CCTV_Off"})
  SF_StopScreenEffect(sm, {resourceName = "CCTV_Effect"})
  SF_SuppressCloseUpCamera(sm, {})
  SF_DebugText(sm, {text = "Bye, world!",
      duration = 5.0})
end

function Scene02_spookylibrary_cctv_State_4(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6078)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_Cam_Library_50"}) then
    if SF_FirstTime(sm, {uniqueID = 1}) then
      if SF_IsCallforwardEqualTo(sm, {callforward = "ChoseSpider",
          dataType = "Bool",
          type = "General"}) then
        SF_StartActivity(sm, {activities = "A_cctv_tarantula"})
      end
    end
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,2)
    return
  end
  if SF_IsActivityComplete(sm, {activity = "A_chrisrevelation"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,6)
    return
  end
end

function Scene02_spookylibrary_cctv_State_5(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6080)
  if not SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_Cam_Library_50"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,3)
    return
  end
end

function Scene02_spookylibrary_cctv_State_6(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6084)
  --If there was anything after the following End-command in the statemachine, that
  --was dropped since it's not allowed in lua to have anything after a return
  SF_End(sm, {statemachine = nil})
  return
end

function Scene02_spookylibrary_cctv_State_7(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8508)
  SF_Comment(sm, {comment = "JM: Moved cockroach logic to coretext"})
end


-- Register the statemachine with the SM handler
gRegisteredStatemachines.Scene02_spookylibrary_cctv = {
    startState = 1,
    name = 'Scene02_spookylibrary_cctv',
    states = {
      {nextState = 4,	func = Scene02_spookylibrary_cctv_State_1, 	funcList = 'Start'},
      {nextState = 5,	func = Scene02_spookylibrary_cctv_State_2, 	funcList = 'Send Sound Event Postprocess_CCTV_On , Start CCTV_Effect effect, Suppress Close Up Camera, Debug Text - Hello, world! '},
      {nextState = 4,	func = Scene02_spookylibrary_cctv_State_3, 	funcList = 'Send Sound Event Postprocess_CCTV_Off , Stop CCTV_Effect effect, Unsuppress Close Up Camera, Debug Text - Bye, world! '},
      {nextState = nil,	func = Scene02_spookylibrary_cctv_State_4, 	funcList = 'Is Player In Trigger T_Cam_Library_50, Is Activity A_chrisrevelation Complete?'},
      {nextState = nil,	func = Scene02_spookylibrary_cctv_State_5, 	funcList = 'NOT Is Player In Trigger T_Cam_Library_50'},
      {nextState = nil,	func = Scene02_spookylibrary_cctv_State_6, 	funcList = 'End '},
      {nextState = nil,	func = Scene02_spookylibrary_cctv_State_7, 	funcList = 'JM: Moved cockroach logic to coretext'},
    },
  } -- Done registering the SM)lua";
    const auto polls = scripts::ParsePolls(script, L"scene02_spookylibrary_cctv");
    const auto cam = std::find_if(polls.begin(), polls.end(), [](const Poll& p) { return p.trigger == L"T_Cam_Library_50" && !p.negated; });
    REQUIRE(cam != polls.end());
    CHECK(cam->waits);
    CHECK(cam->leaves);
    // The tarantula is on the way, but the scene stands where it stood, asking again.
    CHECK(Has(cam->activities, L"A_cctv_tarantula"));
    CHECK_FALSE(cam->story);
    Waits waits;
    waits.SetPolls(polls);
    waits.Asked(10.0, L"T_Cam_Library_50", true);
    CHECK(waits.Judge(10.2, L"T_Cam_Library_50", L"Chris").kind == Waits::Kind::NoStory);
}

TEST_CASE("scripts: the cabin's scene waits for Mike to walk on once Jessica is taken, 2026-11-06")
{
    // act2_5's scene08_insidethecabin.lua as the game ships it, its states 29, 50, 51 and 52. Mike was
    // handed over on the outro path at 22:31:33 (research/sessions/s47-full.log) with nothing listed,
    // "nothing nearby to walk to", until he happened to move at 22:32:21 and the scene went on.
    const std::wstring script = LR"lua(
function Scene08_insidethecabin_State_29(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4154)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_JessicaTaken"}) then
    SF_SetCharacterInteractionDiscoveryParameters(sm, {discoveryParameters = "Head",
        characters = "Player"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,51)
    return
  end
end

function Scene08_insidethecabin_State_50(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6796)
  SF_StartActivity(sm, {activities = "A_JessicaGrabbed"})
  SF_TurnOffCurrentObjective(sm, {})
end

function Scene08_insidethecabin_State_51(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8168)
  if SF_IsCharacterMoving(sm, {character = "Mike",
      threshold = 0.1}) then
    if SF_Wait(sm, {seconds = 0.75,
        uniqueID = 1}) then
      -- Auto-generated JumpToState(), since the condition has a link
      M_JumpToState(sm,50)
      return
    end
  end
end

function Scene08_insidethecabin_State_52(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8170)
  SF_TurnOffCurrentObjective(sm, {})
end

gRegisteredStatemachines.Scene08_insidethecabin = {
    startState = 1,
    name = 'Scene08_insidethecabin',
    states = {
      {nextState = nil,	func = Scene08_insidethecabin_State_29, 	funcList = 'Is Player In Trigger T_JessicaTaken'},
      {nextState = 40,	func = Scene08_insidethecabin_State_50, 	funcList = 'Start Activity A_JessicaGrabbed, Turn Off Current Objective'},
      {nextState = nil,	func = Scene08_insidethecabin_State_51, 	funcList = 'Is Mike moving at a speed of at least 0.1'},
      {nextState = nil,	func = Scene08_insidethecabin_State_52, 	funcList = 'Turn Off Current Objective'},
    },
  } -- Done registering the SM
)lua";
    const auto waits = scripts::ParseMoveWaits(script, L"scene08_insidethecabin");
    REQUIRE(waits.size() == 1);
    CHECK(waits[0].machine == L"Scene08_insidethecabin");
    CHECK(waits[0].state == 51);
    CHECK(waits[0].who == L"Mike");
    CHECK_FALSE(waits[0].negated);
    CHECK(waits[0].story);
    // The machine stands in 51 (the scripts' own record): the scene waits for Mike to walk on.
    CHECK(scripts::WaitsToMove(waits, {{L"Scene08_insidethecabin", 51}, {L"Dialogue_control", 4}}, L"Mike"));
    // In 29, still waiting for the volume: not yet; and another character is not asked about.
    CHECK_FALSE(scripts::WaitsToMove(waits, {{L"Scene08_insidethecabin", 29}}, L"Mike"));
    CHECK_FALSE(scripts::WaitsToMove(waits, {{L"Scene08_insidethecabin", 51}}, L"Jessica"));
}

TEST_CASE("scripts: a wait for stillness is no wait to walk on")
{
    // act5_2's security light: the countdown starts once neither Emily nor Matt moves.
    const std::wstring script = LR"lua(
function Scene02_tower_ascent_securitylight_State_5(sm)
  if not SF_IsCharacterMoving(sm, {character = "Emily",
      threshold = 0.1}) then
    if not SF_IsCharacterMoving(sm, {character = "Matt",
        threshold = 0.1}) then
      M_JumpToState(sm,8)
      return
    end
  end
end
function Scene02_tower_ascent_securitylight_State_8(sm)
  SF_StartActivity(sm, {activities = "A_security_lights_alone"})
end
gRegisteredStatemachines.Scene02_tower_ascent_securitylight = {
    startState = 1,
    name = 'Scene02_tower_ascent_securitylight',
    states = {
      {nextState = nil,	func = Scene02_tower_ascent_securitylight_State_5, 	funcList = 'NOT Is Emily moving'},
      {nextState = nil,	func = Scene02_tower_ascent_securitylight_State_8, 	funcList = 'Start'},
    },
  }
)lua";
    const auto waits = scripts::ParseMoveWaits(script, L"securitylight");
    REQUIRE(waits.size() == 2);
    for (const auto& w : waits)
    {
        CHECK(w.negated);
        CHECK_FALSE(w.story);
    }
    CHECK_FALSE(scripts::WaitsToMove(waits, {{L"Scene02_tower_ascent_securitylight", 5}}, L"Emily"));
}

TEST_CASE("scripts: a loop whose way back moves the story on is no gag")
{
    // Walking in starts one scene; leaving starts another before the machine asks again: each visit
    // moves the story on, so the volume stays a way on.
    const std::wstring script = LR"lua(
function Scene_loop_State_2(sm)
  SF_SendSoundEvent(sm, {value = "x"})
end
function Scene_loop_State_3(sm)
  SF_SendSoundEvent(sm, {value = "y"})
end
function Scene_loop_State_4(sm)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_x"}) then
    SF_StartActivity(sm, {activities = "A_first"})
    M_JumpToState(sm,2)
    return
  end
end
function Scene_loop_State_5(sm)
  if not SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_x"}) then
    SF_StartActivity(sm, {activities = "A_second"})
    M_JumpToState(sm,3)
    return
  end
end
gRegisteredStatemachines.Scene_loop = {
    startState = 4,
    name = 'Scene_loop',
    states = {
      {nextState = 5,	func = Scene_loop_State_2, 	funcList = ''},
      {nextState = 4,	func = Scene_loop_State_3, 	funcList = ''},
      {nextState = nil,	func = Scene_loop_State_4, 	funcList = ''},
      {nextState = nil,	func = Scene_loop_State_5, 	funcList = ''},
    },
  }
)lua";
    const auto polls = scripts::ParsePolls(script, L"loop");
    const auto in = std::find_if(polls.begin(), polls.end(), [](const Poll& p) { return p.trigger == L"T_x" && !p.negated; });
    REQUIRE(in != polls.end());
    CHECK(in->story);
}

TEST_CASE("scripts: a trigger starting an activity of the background moves nothing on, act3_4's rats, 2026-11-07")
{
    // act3_4, by the hunger totem and the mine's note: BP_CharacterTriggerVolume_C_7's
    // BatesStartSequenceTriggerAction starts BatesActivityActor_5 of LV_Scene04_dark_cave,
    // PAO_rat_CaveFloor (bIsBackgroundActivity, bIsPAOSequence: rats on the cave's floor). Listed as
    // a way on, it was walked to and nothing came of it.
    CHECK_FALSE(scripts::StoryActor(L"PAO_rat_CaveFloor", true, true));
    CHECK_FALSE(scripts::StoryActor(L"PAO_rat_CaveFloor", false, false));
    CHECK_FALSE(scripts::StoryActor(L"A_rats", true, false));
    CHECK_FALSE(scripts::StoryActor(L"A_rats", false, true));
    // The story's own activities, and one with no name to judge by.
    CHECK(scripts::StoryActor(L"A_wakeup", false, false));
    CHECK(scripts::StoryActor(L"", false, false));
}

TEST_CASE("scripts: a volume the scene keeps waiting in while it turns a thing on moves the scene on: act4_3's ladder, 2026-11-07")
{
    // act4_3's scene03_look_for_map.lua as the game ships it, its states 11, 12, 13 and 20. After Emily
    // is helped up, state 12 asks whether Matt stands in T_Cam_station_Interior_20b by the ladder, and
    // state 11, while he does, waits for her to pick up her torch and turns on the climb down
    // (research/sessions/s54-full.log: the volume judged nothing of the story's, Matt stood about the
    // station with nothing to walk to).
    const std::wstring script = LR"lua(function Scene03_look_for_map_State_11(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4776)
  if not SF_IsInTrigger(sm, {gameObjects = "Matt",
      trigger = "T_Cam_station_Interior_20b"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,12)
    return
  end
  SF_SetCallforward(sm, {callforward = "MattInLadderTrigger",
      valueBool = true,
      dataType = "Bool",
      type = "General"})
  if SF_IsActivityComplete(sm, {activity = "A_EmilyPicksUpTorch_PAO"}) then
    if SF_Wait(sm, {seconds = 1.0,
        uniqueID = 1}) then
      -- Auto-generated JumpToState(), since the condition has a link
      M_JumpToState(sm,20)
      return
    end
  end
end

function Scene03_look_for_map_State_12(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4778)
  SF_SetCallforward(sm, {callforward = "MattInLadderTrigger",
      dataType = "Bool",
      type = "General"})
  if SF_IsInTrigger(sm, {gameObjects = "Matt",
      trigger = "T_Cam_station_Interior_20b"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,11)
    return
  end
end

function Scene03_look_for_map_State_13(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 4799)
  if SF_IsBoundToActivity(sm, {characters = "Matt",
      activity = "I_climb_down"}) then
    SF_SendMessageToActivity(sm, {activities = "A_emily_on_ledge",
        message = "End"})
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,9)
    return
  end
end

function Scene03_look_for_map_State_20(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 8908)
  SF_SetEnableActivity(sm, {activities = "I_climb_down",
      enable = true})
end


-- Register the statemachine with the SM handler
gRegisteredStatemachines.Scene03_look_for_map = {
    startState = 1,
    name = 'Scene03_look_for_map',
    states = {
      {nextState = 2,	func = Scene03_look_for_map_State_1, 	funcList = 'Start'},
      {nextState = 7,	func = Scene03_look_for_map_State_2, 	funcList = 'Disable Activity I_help_emily, Disable Activity I_climb_down, Enable Activity I_graffiti, Run Statemachine Scene03_look_for_map_ai, Disable Activity I_lookatmap_without_emily, Disable Activity I_lookatmap_with_emily, Set Light Visibilty'},
      {nextState = nil,	func = Scene03_look_for_map_State_3, 	funcList = 'Is Activity A_make_plan Complete?'},
      {nextState = 3,	func = Scene03_look_for_map_State_4, 	funcList = 'Start Activity A_make_plan'},
      {nextState = nil,	func = Scene03_look_for_map_State_5, 	funcList = 'End '},
      {nextState = nil,	func = Scene03_look_for_map_State_6, 	funcList = 'Is Activity I_help_emily Complete?'},
      {nextState = 10,	func = Scene03_look_for_map_State_7, 	funcList = 'Is Start Node = 3, Is Start Node = 31, Is Start Node = 32, Is Start Node = 33'},
      {nextState = 11,	func = Scene03_look_for_map_State_8, 	funcList = 'Allow Player Locomotion, Swap Loco Anim Set Of Emily to anim_set_emily_held_item [blend of 0]'},
      {nextState = nil,	func = Scene03_look_for_map_State_9, 	funcList = 'Is Activity I_ledge_shimmy Complete?'},
      {nextState = nil,	func = Scene03_look_for_map_State_10, 	funcList = 'Is Activity I_map Complete?'},
      {nextState = nil,	func = Scene03_look_for_map_State_11, 	funcList = 'NOT Is Matt In Trigger T_Cam_station_Interior_20b, Set Callforward MattInLadderTrigger True, Is Activity A_EmilyPicksUpTorch_PAO Complete?'},
      {nextState = nil,	func = Scene03_look_for_map_State_12, 	funcList = 'Set Callforward MattInLadderTrigger False, Is Matt In Trigger T_Cam_station_Interior_20b'},
      {nextState = nil,	func = Scene03_look_for_map_State_13, 	funcList = 'Is Matt Bound To Activity I_climb_down'},
      {nextState = 18,	func = Scene03_look_for_map_State_14, 	funcList = 'Set Callforward CableCarSway True'},
      {nextState = nil,	func = Scene03_look_for_map_State_15, 	funcList = 'Is Activity A_cablecarmoved Complete?'},
      {nextState = nil,	func = Scene03_look_for_map_State_16, 	funcList = 'Give Held Item HI_emilys_torch To Emily With Style Pointing Left Hand Ready'},
      {nextState = nil,	func = Scene03_look_for_map_State_17, 	funcList = 'Put A_EmilyPicksUpTorch_PAO  to sleep.'},
      {nextState = nil,	func = Scene03_look_for_map_State_18, 	funcList = 'Is Activity A_ledge_emily_happy Complete?, Is Activity A_ledge_emily_angry Complete?'},
      {nextState = nil,	func = Scene03_look_for_map_State_19, 	funcList = 'Put A_emily_on_ledge  to sleep.'},
      {nextState = 13,	func = Scene03_look_for_map_State_20, 	funcList = 'Enable Activity I_climb_down'},
      {nextState = nil,	func = Scene03_look_for_map_State_21, 	funcList = '#BM - Guillermo. We wait 1 second to prevent I_climb_down to be done before A_emily_on_ledge starts'},
    },
  } -- Done registering the SM)lua";
    const auto polls = scripts::ParsePolls(script, L"scene03_look_for_map");
    const auto in = std::find_if(polls.begin(), polls.end(), [](const Poll& p) { return p.trigger == L"T_Cam_station_Interior_20b" && !p.negated; });
    REQUIRE(in != polls.end());
    CHECK(in->state == 12);
    CHECK(in->waits);
    CHECK(in->leaves);
    CHECK(in->story);
    // Leaving moves nothing on: the scene goes back to asking.
    const auto out = std::find_if(polls.begin(), polls.end(), [](const Poll& p) { return p.trigger == L"T_Cam_station_Interior_20b" && p.negated; });
    REQUIRE(out != polls.end());
    CHECK_FALSE(out->story);
    Waits waits;
    waits.SetPolls(polls);
    waits.Asked(10.0, L"T_Cam_station_Interior_20b", true);
    CHECK(waits.Judge(10.2, L"T_Cam_station_Interior_20b", L"Matt").kind == Waits::Kind::Waiting);
}

TEST_CASE("scripts: a scene that turns a thing on once she stands in a volume waits for her there")
{
    const std::wstring script = LR"lua(function Scene01_turnon_State_1(sm)
  SF_Start(sm, {})
end

function Scene01_turnon_State_2(sm)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_bench"}) then
    SF_SetEnableActivity(sm, {activities = "I_sit",
        enable = true})
    M_JumpToState(sm,3)
    return
  end
end

function Scene01_turnon_State_3(sm)
  if SF_IsActivityComplete(sm, {activity = "I_sit"}) then
    M_JumpToState(sm,4)
    return
  end
end

function Scene01_turnon_State_4(sm)
  SF_End(sm, {statemachine = nil})
  return
end

gRegisteredStatemachines.Scene01_turnon = {
    startState = 1,
    name = 'Scene01_turnon',
    states = {
      {nextState = 2,	func = Scene01_turnon_State_1, 	funcList = 'Start'},
      {nextState = nil,	func = Scene01_turnon_State_2, 	funcList = 'Is Player In Trigger T_bench, Enable Activity I_sit'},
      {nextState = nil,	func = Scene01_turnon_State_3, 	funcList = 'Is Activity I_sit Complete?'},
      {nextState = nil,	func = Scene01_turnon_State_4, 	funcList = 'End '},
    },
  } -- Done registering the SM)lua";
    const auto polls = scripts::ParsePolls(script, L"scene01_turnon");
    REQUIRE(polls.size() == 1);
    CHECK(polls[0].story);
    // A volume walked into once, the scene then waiting on something else that turns a thing on,
    // asks nothing of her standing there: what follows is not hers to bring about.
    const std::wstring passing = LR"lua(function Scene01_passing_State_1(sm)
  SF_Start(sm, {})
end

function Scene01_passing_State_2(sm)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_hall"}) then
    M_JumpToState(sm,3)
    return
  end
end

function Scene01_passing_State_3(sm)
  if SF_IsActivityComplete(sm, {activity = "A_knock"}) then
    M_JumpToState(sm,4)
    return
  end
end

function Scene01_passing_State_4(sm)
  SF_SetEnableActivity(sm, {activities = "I_door",
      enable = true})
end

gRegisteredStatemachines.Scene01_passing = {
    startState = 1,
    name = 'Scene01_passing',
    states = {
      {nextState = 2,	func = Scene01_passing_State_1, 	funcList = 'Start'},
      {nextState = nil,	func = Scene01_passing_State_2, 	funcList = 'Is Player In Trigger T_hall'},
      {nextState = nil,	func = Scene01_passing_State_3, 	funcList = 'Is Activity A_knock Complete?'},
      {nextState = nil,	func = Scene01_passing_State_4, 	funcList = 'Enable Activity I_door'},
    },
  } -- Done registering the SM)lua";
    const auto passingPolls = scripts::ParsePolls(passing, L"scene01_passing");
    REQUIRE(passingPolls.size() == 1);
    CHECK_FALSE(passingPolls[0].story);
    // Turning it off is no more than a look: nothing comes of standing there.
    std::wstring off = script;
    const std::wstring enable = L",\n        enable = true";
    off.replace(off.find(enable), enable.size(), L"");
    const auto offPolls = scripts::ParsePolls(off, L"scene01_turnon");
    REQUIRE(offPolls.size() == 1);
    CHECK_FALSE(offPolls[0].story);
}

TEST_CASE("scripts: a volume that turns a thing off while she stands in it: Jack's cigar box, 2026-11-07")
{
    // act4_2's T_disable_cigar_box-Enter and -Leave as the game ships them. Five walks to the cigar box
    // went in through the volume, the game turned the box off a metre or two short, and each walk
    // ended saying the target was gone (research/sessions/s54-full.log).
    const std::wstring enter = LR"lua(--------------------------------------------------------------
-- SMED events for triggers 'T_disable_cigar_box' - 'Enter' --
--------------------------------------------------------------

-- Create entry in table for this event
if gEvents["EnterTrigger"] == nil then
	gEvents["EnterTrigger"] = {}
end

--Add it to the events table
gEvents["EnterTrigger"]["T_disable_cigar_box"] = {
	stateMachine = "Event_t_disable_cigar_box_enter",
	triggerLimit = -1,
	delay = 0,
}
-- Here's the actual statemachine
------------------------------------------------------------------
-- Statemachine definitions for event T_disable_cigar_box Enter --
------------------------------------------------------------------
function Event_T_disable_cigar_box_Enter_State_1(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6542)
  SF_Start(sm, {})
end

function Event_T_disable_cigar_box_Enter_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6544)
  SF_SetEnableActivity(sm, {activities = "I_jackslair_cigarbox"})
end

function Event_T_disable_cigar_box_Enter_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6546)
  --If there was anything after the following End-command in the statemachine, that
  --was dropped since it's not allowed in lua to have anything after a return
  SF_End(sm, {statemachine = nil})
  return
end


-- Register the statemachine with the SM handler
gRegisteredStatemachines.Event_t_disable_cigar_box_enter = {
    startState = 1,
    name = 'Event_t_disable_cigar_box_enter',
    states = {
      {nextState = 2,	func = Event_T_disable_cigar_box_Enter_State_1, 	funcList = 'Start'},
      {nextState = 3,	func = Event_T_disable_cigar_box_Enter_State_2, 	funcList = 'Disable Activity I_jackslair_cigarbox'},
      {nextState = nil,	func = Event_T_disable_cigar_box_Enter_State_3, 	funcList = 'End '},
    },
  } -- Done registering the SM)lua";
    const std::wstring leave = LR"lua(--------------------------------------------------------------
-- SMED events for triggers 'T_disable_cigar_box' - 'Leave' --
--------------------------------------------------------------

-- Create entry in table for this event
if gEvents["LeaveTrigger"] == nil then
	gEvents["LeaveTrigger"] = {}
end

--Add it to the events table
gEvents["LeaveTrigger"]["T_disable_cigar_box"] = {
	stateMachine = "Event_t_disable_cigar_box_leave",
	triggerLimit = -1,
	delay = 0,
}
-- Here's the actual statemachine
------------------------------------------------------------------
-- Statemachine definitions for event T_disable_cigar_box Leave --
------------------------------------------------------------------
function Event_T_disable_cigar_box_Leave_State_1(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6536)
  SF_Start(sm, {})
end

function Event_T_disable_cigar_box_Leave_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6539)
  SF_SetEnableActivity(sm, {activities = "I_jackslair_cigarbox",
      enable = true})
end

function Event_T_disable_cigar_box_Leave_State_3(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 6540)
  --If there was anything after the following End-command in the statemachine, that
  --was dropped since it's not allowed in lua to have anything after a return
  SF_End(sm, {statemachine = nil})
  return
end


-- Register the statemachine with the SM handler
gRegisteredStatemachines.Event_t_disable_cigar_box_leave = {
    startState = 1,
    name = 'Event_t_disable_cigar_box_leave',
    states = {
      {nextState = 2,	func = Event_T_disable_cigar_box_Leave_State_1, 	funcList = 'Start'},
      {nextState = 3,	func = Event_T_disable_cigar_box_Leave_State_2, 	funcList = 'Enable Activity I_jackslair_cigarbox'},
      {nextState = nil,	func = Event_T_disable_cigar_box_Leave_State_3, 	funcList = 'End '},
    },
  } -- Done registering the SM)lua";
    const auto in = scripts::ParseActivitySwitches(enter);
    REQUIRE(in.size() == 1);
    CHECK(in[0].trigger == L"T_disable_cigar_box");
    CHECK_FALSE(in[0].leave);
    CHECK(in[0].activity == L"I_jackslair_cigarbox");
    CHECK_FALSE(in[0].enable);
    const auto out = scripts::ParseActivitySwitches(leave);
    REQUIRE(out.size() == 1);
    CHECK(out[0].leave);
    CHECK(out[0].enable);
    std::vector<scripts::ActivitySwitch> all = in;
    all.insert(all.end(), out.begin(), out.end());
    const auto off = scripts::OffInside(all);
    REQUIRE(off.count(L"I_jackslair_cigarbox") == 1);
    CHECK(off.at(L"I_jackslair_cigarbox") == std::vector<std::wstring>{L"T_disable_cigar_box"});
    // Turned off by walking in and never on again by walking out: no volume it is off in only while
    // she stands there.
    CHECK(scripts::OffInside(in).empty());
    // The other way round, on only near (act4_2's T_wendigo_trap_near_enable): not off inside.
    std::vector<scripts::ActivitySwitch> near = {{L"T_wendigo_trap_near_enable", false, L"I_hospital_trap_fingers", true},
                                                 {L"T_wendigo_trap_near_enable", true, L"I_hospital_trap_fingers", false}};
    CHECK(scripts::OffInside(near).empty());
    // A script of no trigger has no switches.
    CHECK(scripts::ParseActivitySwitches(L"SF_SetEnableActivity(sm, {activities = \"I_x\"})").empty());
}

namespace
{
    // act5_2's scene03_calling_for_help_ai.lua, the companion's machine in the radio tower: Emily
    // walking into T_Tower_FuseBox_To_Telescope has Matt lead her to the radio.
    const std::wstring kTowerMatt = LR"lua(
function Scene03_calling_for_help_ai_State_10(sm)
  Debug_SetSMEDStateID(sm, 5300)
  if SF_IsBoundToActivity(sm, {characters = "Emily",
      activity = "I_UseCurcuitBreaker"}) then
    M_JumpToState(sm,16)
    return
  end
end
function Scene03_calling_for_help_ai_State_16(sm)
  Debug_SetSMEDStateID(sm, 5330)
  SF_OrderForceIdle(sm, {character = "Matt"})
end
function Scene03_calling_for_help_ai_State_17(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5350)
  SF_Comment(sm, {comment = "Power on, Player has moved far enough away from the door that LEAD orders are ok to use"})
  SF_OrderLeadTo(sm, {lead = "Player",
      alterMovementStyle = true,
      character = "Matt",
      allowTurningWhileWaiting = true,
      useMarkerHeading = true,
      marker = "M_ai_TowerInside_Radio",
      moveType = "Marker",
      movementStyle = "Lead_3m"})
end
function Scene03_calling_for_help_ai_State_18(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5353)
  if SF_IsInTrigger(sm, {gameObjects = "Player",
      trigger = "T_Tower_FuseBox_To_Telescope"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,17)
    return
  end
end
gRegisteredStatemachines.Scene03_calling_for_help_ai = {
    startState = 18,
    name = 'Scene03_calling_for_help_ai',
    states = {
      {nextState = nil,	func = Scene03_calling_for_help_ai_State_10, 	funcList = 'Is Emily Bound To Activity I_UseCurcuitBreaker'},
      {nextState = 11,	func = Scene03_calling_for_help_ai_State_16, 	funcList = 'Emily interacting with circuit breaker, Force Matt to idle.'},
      {nextState = 10,	func = Scene03_calling_for_help_ai_State_17, 	funcList = 'Power on, Order Matt to Lead Player to M_ai_TowerInside_Radio using Lead_3m'},
      {nextState = nil,	func = Scene03_calling_for_help_ai_State_18, 	funcList = 'Is Player In Trigger T_Tower_FuseBox_To_Telescope'},
    },
  }
)lua";

    // act5_2's scene01_deer_confrontation_deerwall.lua: the deer's scene waits for Matt, the
    // player then, in the herd's first stage.
    const std::wstring kDeerWall = LR"lua(
function Scene01_deer_confrontation_deerwall_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 5986)
  if SF_IsInTrigger(sm, {gameObjects = "Matt",
      trigger = "T_DeerWall_Stage1"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,6)
    return
  end
end
function Scene01_deer_confrontation_deerwall_State_3(sm)
  Debug_SetSMEDStateID(sm, 5988)
  if SF_IsInTrigger(sm, {gameObjects = "Matt",
      trigger = "T_DeerWall_Stage2"}) then
    SF_SendMessageToActivity(sm, {activities = "A_deer_wall",
        message = "Activate"})
    M_JumpToState(sm,4)
    return
  end
end
function Scene01_deer_confrontation_deerwall_State_4(sm)
  Debug_SetSMEDStateID(sm, 5990)
  SF_End(sm, {statemachine = nil})
  return
end
function Scene01_deer_confrontation_deerwall_State_6(sm)
  Debug_SetSMEDStateID(sm, 5996)
  SF_SendMessageToActivity(sm, {activities = "A_deer_wall",
      message = "Activate"})
end
gRegisteredStatemachines.Scene01_deer_confrontation_deerwall = {
    startState = 1,
    name = 'Scene01_deer_confrontation_deerwall',
    states = {
      {nextState = nil,	func = Scene01_deer_confrontation_deerwall_State_2, 	funcList = 'Is Matt In Trigger T_DeerWall_Stage1'},
      {nextState = nil,	func = Scene01_deer_confrontation_deerwall_State_3, 	funcList = 'Is Matt In Trigger T_DeerWall_Stage2'},
      {nextState = nil,	func = Scene01_deer_confrontation_deerwall_State_4, 	funcList = 'End '},
      {nextState = 3,	func = Scene01_deer_confrontation_deerwall_State_6, 	funcList = 'Send Activate Message To Activity A_deer_wall'},
    },
  }
)lua";

    // Act2_2's scene03_gotomine.lua: the story waits for Jessica at the mine's choice.
    const std::wstring kGoToMine = LR"lua(
function Scene03_gotomine_State_2(sm)
-- Set what the last run state is for SMED debugging
  Debug_SetSMEDStateID(sm, 361)
  if SF_IsInTrigger(sm, {gameObjects = "Jessica",
      trigger = "T_EnterMineChoice"}) then
    -- Auto-generated JumpToState(), since the condition has a link
    M_JumpToState(sm,3)
    return
  end
end
function Scene03_gotomine_State_3(sm)
  Debug_SetSMEDStateID(sm, 364)
  SF_StartActivity(sm, {activities = "A_EnterMineChoice"})
end
function Scene03_gotomine_State_4(sm)
  Debug_SetSMEDStateID(sm, 366)
  if SF_IsActivityComplete(sm, {activity = "A_EnterMineChoice"}) then
    M_JumpToState(sm,6)
    return
  end
end
gRegisteredStatemachines.Scene03_gotomine = {
    startState = 1,
    name = 'Scene03_gotomine',
    states = {
      {nextState = nil,	func = Scene03_gotomine_State_2, 	funcList = 'Is Jessica In Trigger T_EnterMineChoice'},
      {nextState = 4,	func = Scene03_gotomine_State_3, 	funcList = 'Start Activity A_EnterMineChoice'},
      {nextState = nil,	func = Scene03_gotomine_State_4, 	funcList = 'Is Activity A_EnterMineChoice Complete?'},
    },
  }
)lua";

    std::vector<Poll> Level(std::initializer_list<std::pair<const std::wstring*, const wchar_t*>> scripts)
    {
        std::vector<Poll> all;
        for (const auto& [script, source] : scripts)
        {
            auto polls = scripts::ParsePolls(*script, source);
            all.insert(all.end(), polls.begin(), polls.end());
        }
        return all;
    }
}

TEST_CASE("scripts: a companion sent on moves the scene on only while a scene waits for her, the radio tower, 2026-11-08")
{
    // Alone in its level the tower's question moves the scene on only by sending Matt on, and no
    // scene of the level waits for Matt: it is no way on.
    auto alone = Level({{&kTowerMatt, L"scene03_calling_for_help_ai"}});
    const Poll* lead = Find(alone, L"T_Tower_FuseBox_To_Telescope");
    REQUIRE(lead);
    CHECK(lead->story);
    CHECK(lead->sentOnly);
    CHECK(Has(lead->sentOn, L"Matt"));
    CHECK(scripts::WeighCompanions(alone) == 1);
    CHECK_FALSE(Find(alone, L"T_Tower_FuseBox_To_Telescope")->story);
    // In act5_2 the deer's scene waits for Matt, while he is the player, long before: the question
    // stands, but moves the scene on only while that wait is asked about someone else.
    auto act = Level({{&kTowerMatt, L"scene03_calling_for_help_ai"}, {&kDeerWall, L"scene01_deer_confrontation_deerwall"}});
    CHECK(scripts::WeighCompanions(act) == 0);
    scripts::Waits waits;
    waits.SetPolls(act);
    waits.Asked(100.0, L"T_Tower_FuseBox_To_Telescope", true);
    CHECK(waits.Judge(100.2, L"T_Tower_FuseBox_To_Telescope", L"Emily").kind == scripts::Waits::Kind::NoStory);
    // Were the deer's scene asking about Matt then, sending him on would move it on.
    waits.Asked(100.1, L"T_DeerWall_Stage1", false);
    CHECK(waits.Judge(100.2, L"T_Tower_FuseBox_To_Telescope", L"Emily").kind == scripts::Waits::Kind::Waiting);
    // Not while the player is Matt himself.
    CHECK(waits.Judge(100.2, L"T_Tower_FuseBox_To_Telescope", L"Matt").kind == scripts::Waits::Kind::NoStory);
}

TEST_CASE("scripts: Jessica led on to the mine moves the scene on while the story waits for her at its choice")
{
    auto act = Level({{&kJessicaLoop, L"aicontroller"}, {&kGoToMine, L"scene03_gotomine"}});
    const Poll* mine = Find(act, L"T_MineEntrance");
    REQUIRE(mine);
    CHECK(mine->sentOnly);
    CHECK(Has(mine->sentOn, L"Jessica"));
    const Poll* choice = Find(act, L"T_EnterMineChoice");
    REQUIRE(choice);
    CHECK(choice->story);
    CHECK_FALSE(choice->sentOnly);
    CHECK(scripts::WeighCompanions(act) == 0);
    CHECK(Find(act, L"T_MineEntrance")->story);
    scripts::Waits waits;
    waits.SetPolls(act);
    waits.Asked(10.0, L"T_MineEntrance", true);
    waits.Asked(10.0, L"T_EnterMineChoice", false);
    CHECK(waits.Judge(10.5, L"T_MineEntrance", L"Mike").kind == scripts::Waits::Kind::Waiting);
    // The choice's wait no longer asked (the choice made), the entrance moves nothing on.
    waits.Asked(20.0, L"T_MineEntrance", true);
    CHECK(waits.Judge(20.5, L"T_MineEntrance", L"Mike").kind == scripts::Waits::Kind::NoStory);
}

TEST_CASE("scripts: a wait the game answers yes is met, though no look saw her in the volume: the cellar's ghost scare, 2026-11-08")
{
    // research/sessions/s58-whole.log: Scene01_doorghostscare's state 7 asked whether the player
    // stood in T_door_ghost_scare while the game held Ashley to the path toward the creaky door
    // (11:26:55); she walked in, the scare took her at 11:27:41 between two of the mod's looks, and
    // after it the volume was held as a way on for ten minutes ("the scene having waited on it and
    // left off asking for now"), in and out of the list as she walked the cellar.
    using Kind = Waits::Kind;
    Poll scare;
    scare.source = L"scene01_doorghostscare";
    scare.machine = L"Scene01_doorghostscare";
    scare.state = 7;
    scare.trigger = L"T_door_ghost_scare";
    scare.who = L"Player";
    scare.story = true;
    scare.calls = {L"SF_StartActivity", L"SF_RunStatemachine"};
    Waits waits;
    waits.SetPolls({scare});
    scripts::HeldWaits held;
    // 11:26:55 to 11:27:41: asked at every update, answered no; she is not in it at any look.
    for (double t = 1000.0; t < 1046.0; t += 0.5)
    {
        waits.Asked(t, L"T_door_ghost_scare", true);
        waits.Answered(t, L"T_door_ghost_scare", false);
    }
    CHECK(waits.Judge(1045.8, L"T_door_ghost_scare", L"Ashley").kind == Kind::Waiting);
    CHECK(held.Waiting(L"T_door_ghost_scare", Kind::Waiting, false));
    // 11:27:41: she walks in; the game answers yes, and the machine moves on and asks no more.
    waits.Asked(1046.2, L"T_door_ghost_scare", true);
    waits.Answered(1046.2, L"T_door_ghost_scare", true);
    // At the next look, within a second of the ask, the scene may ask on (a state that asks while she
    // stands in it): still waiting, the way held.
    CHECK(waits.Judge(1046.4, L"T_door_ghost_scare", L"Ashley").kind == Kind::Waiting);
    CHECK(held.Waiting(L"T_door_ghost_scare", Kind::Waiting, false));
    // A second on with no question since the yes: the wait is met, no longer held.
    const auto met = waits.Judge(1047.3, L"T_door_ghost_scare", L"Ashley");
    CHECK(met.kind == Kind::Met);
    CHECK_FALSE(held.Waiting(L"T_door_ghost_scare", met.kind, false));
    // 11:28:01, after the scare, the machine silent: no way on.
    CHECK(waits.Judge(1066.0, L"T_door_ghost_scare", L"Ashley").kind == Kind::Met);
    CHECK_FALSE(held.Waiting(L"T_door_ghost_scare", Kind::Met, false));
    CHECK_FALSE(held.Held(L"T_door_ghost_scare"));
    // Asked again and answered no (a new wait of the scene's): waiting again.
    waits.Asked(1200.0, L"T_door_ghost_scare", true);
    waits.Answered(1200.0, L"T_door_ghost_scare", false);
    CHECK(waits.Judge(1200.3, L"T_door_ghost_scare", L"Ashley").kind == Kind::Waiting);
    // A state that asks on while she stands in it, answered yes each time (a compound condition not
    // yet met), waits on.
    Waits asksOn;
    asksOn.SetPolls({scare});
    for (double t = 2000.0; t < 2005.0; t += 0.5)
    {
        asksOn.Asked(t, L"T_door_ghost_scare", true);
        asksOn.Answered(t, L"T_door_ghost_scare", true);
    }
    CHECK(asksOn.Judge(2004.7, L"T_door_ghost_scare", L"Ashley").kind == Kind::Waiting);
    // A question never answered yes is no met wait.
    Waits fresh;
    fresh.SetPolls({scare});
    CHECK(fresh.Judge(10.0, L"T_door_ghost_scare", L"Ashley").kind == Kind::NotNow);
    fresh.Asked(10.0, L"T_door_ghost_scare", true);
    CHECK(fresh.Judge(10.2, L"T_door_ghost_scare", L"Ashley").kind == Kind::Waiting);
}

TEST_CASE("scripts: a scene's wait stands only while its machine stands in it, whoever else asks: act7_2, 2026-11-09")
{
    // research/sessions/s63-play.log 00:19:20: the dialogue's machine asked at every update whether
    // Mike stood in T_wendigo_sighting, to play a line; the scene's own wait for it was states away.
    Poll story;
    story.source = L"scene03_not_alone";
    story.machine = L"Scene03_not_alone";
    story.state = 35;
    story.trigger = L"T_wendigo_sighting";
    story.who = L"Player";
    story.waits = true;
    story.leaves = true;
    story.story = true;
    Poll line = story;
    line.source = L"dialogue_control";
    line.machine = L"Dialogue_control";
    line.state = 3;
    line.story = false;
    Waits waits;
    waits.SetPolls({story, line});
    waits.Asked(10.0, L"T_wendigo_sighting", true);
    using Machines = std::vector<std::pair<std::wstring, int>>;
    // The scene stands in another state: the asks are the dialogue's.
    const Machines elsewhere{{L"Scene03_not_alone", 42}, {L"Dialogue_control", 3}};
    CHECK(waits.Judge(10.3, L"T_wendigo_sighting", L"Mike", &elsewhere).kind == Waits::Kind::NotNow);
    // It stands in its wait: the scene waits for him.
    const Machines there{{L"Scene03_not_alone", 35}, {L"Dialogue_control", 3}};
    CHECK(waits.Judge(10.3, L"T_wendigo_sighting", L"Mike", &there).kind == Waits::Kind::Waiting);
    // No record of the machines, or none of this one: judged by the asks, as before.
    CHECK(waits.Judge(10.3, L"T_wendigo_sighting", L"Mike").kind == Waits::Kind::Waiting);
    const Machines others{{L"Dialogue_control", 3}};
    CHECK(waits.Judge(10.3, L"T_wendigo_sighting", L"Mike", &others).kind == Waits::Kind::Waiting);
    // Two of the machine run: one standing in its wait is enough.
    const Machines twice{{L"Scene03_not_alone", 42}, {L"Scene03_not_alone", 35}};
    CHECK(waits.Judge(10.3, L"T_wendigo_sighting", L"Mike", &twice).kind == Waits::Kind::Waiting);
    const Machines first{{L"Scene03_not_alone", 35}, {L"Scene03_not_alone", 42}};
    CHECK(waits.Judge(10.3, L"T_wendigo_sighting", L"Mike", &first).kind == Waits::Kind::Waiting);
}
