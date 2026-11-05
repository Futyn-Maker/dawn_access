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
