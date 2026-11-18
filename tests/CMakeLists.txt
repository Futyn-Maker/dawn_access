cmake_minimum_required(VERSION 3.22)

# The tests of the mod's engine-free modules: a project of its own, so that those modules are
# compiled without UE4SS and stay free of it. scripts\test.cmd configures it into build\tests
# and runs the result.
project(DawnAccessTests LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

get_filename_component(DA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DA_SRC "${DA_ROOT}/src")

add_executable(tests
    Main.cpp
    Support.cpp
    LogStub.cpp
    StringsTests.cpp
    ConfigTests.cpp
    LocaleTests.cpp
    KeySpecTests.cpp
    HotkeyTableTests.cpp
    DescribeTests.cpp
    SymbolsTests.cpp
    GreetingTests.cpp
    FaultTests.cpp
    ExploreTextTests.cpp
    GridPathTests.cpp
    GroundFileTests.cpp
    WayListTests.cpp
    LookTurnTests.cpp
    HoldTests.cpp
    CalmRulesTests.cpp
    QteRulesTests.cpp
    ChoiceRulesTests.cpp
    PromptRulesTests.cpp
    TextRulesTests.cpp
    SaveCopyRulesTests.cpp
    MenuRulesTests.cpp
    ShootingRulesTests.cpp
    NudgeRulesTests.cpp
    SceneScriptsTests.cpp
    ShapesTests.cpp
    RoomsTests.cpp
    SpeechRulesTests.cpp
    WorldStreamTests.cpp
    GroundRecheckTests.cpp
    KeyChoiceTests.cpp
    CreditsRulesTests.cpp
    PictureRulesTests.cpp
    AchievementRulesTests.cpp
    AddressCacheTests.cpp
    "${DA_SRC}/core/Strings.cpp"
    "${DA_SRC}/core/Config.cpp"
    "${DA_SRC}/core/Symbols.cpp"
    "${DA_SRC}/core/Greeting.cpp"
    "${DA_SRC}/core/Fault.cpp"
    "${DA_SRC}/locale/Locale.cpp"
    "${DA_SRC}/hotkeys/KeySpec.cpp"
    "${DA_SRC}/hotkeys/HotkeyTable.cpp"
    "${DA_SRC}/ui/Describe.cpp"
    "${DA_SRC}/features/ExploreText.cpp"
    "${DA_SRC}/features/GridPath.cpp"
    "${DA_SRC}/features/GroundFile.cpp"
    "${DA_SRC}/features/WayList.cpp"
    "${DA_SRC}/features/LookTurn.cpp"
    "${DA_SRC}/features/Hold.cpp"
    "${DA_SRC}/features/CalmRules.cpp"
    "${DA_SRC}/features/QteRules.cpp"
    "${DA_SRC}/features/ChoiceRules.cpp"
    "${DA_SRC}/features/PromptRules.cpp"
    "${DA_SRC}/features/TextRules.cpp"
    "${DA_SRC}/features/MenuRules.cpp"
    "${DA_SRC}/features/ShootingRules.cpp"
    "${DA_SRC}/features/NudgeRules.cpp"
    "${DA_SRC}/features/SceneScripts.cpp"
    "${DA_SRC}/features/Shapes.cpp"
    "${DA_SRC}/features/Rooms.cpp"
    "${DA_SRC}/speech/SpeechRules.cpp"
    "${DA_SRC}/features/WorldStream.cpp"
    "${DA_SRC}/features/GroundRecheck.cpp"
    "${DA_SRC}/input/KeyChoice.cpp"
    "${DA_SRC}/features/CreditsRules.cpp"
    "${DA_SRC}/features/PictureRules.cpp"
    "${DA_SRC}/features/AchievementRules.cpp"
    "${DA_SRC}/diag/SaveCopyRules.cpp")

target_include_directories(tests PRIVATE "${DA_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tests PRIVATE doctest::doctest)
# The repository's root, for the tests that read the shipped ini.
target_compile_definitions(tests PRIVATE
    "DA_REPO_ROOT=\"${DA_ROOT}\""
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE)
set_target_properties(tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(tests PRIVATE /W4 /utf-8 /permissive-)
endif()

# The audit of trigger volumes over a recorded ground (audit\Audit.cpp): no test, a tool built
# from the same engine-free modules, run by hand over the records play leaves.
add_executable(audit
    audit/Audit.cpp
    "${DA_SRC}/core/Strings.cpp"
    "${DA_SRC}/features/GridPath.cpp"
    "${DA_SRC}/features/GroundFile.cpp"
    "${DA_SRC}/features/WayList.cpp"
    "${DA_SRC}/features/Shapes.cpp")
target_include_directories(audit PRIVATE "${DA_SRC}")
target_compile_definitions(audit PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
set_target_properties(audit PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(audit PRIVATE /W4 /utf-8 /permissive-)
endif()

# Every question the scenes' scripts ask about a character in a volume, as the parse judges it
# (audit\ScriptsAudit.cpp): no test, a tool run by hand over the game's exported scripts.
add_executable(scripts_audit
    audit/ScriptsAudit.cpp
    "${DA_SRC}/core/Strings.cpp"
    "${DA_SRC}/features/SceneScripts.cpp")
target_include_directories(scripts_audit PRIVATE "${DA_SRC}")
target_compile_definitions(scripts_audit PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
set_target_properties(scripts_audit PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(scripts_audit PRIVATE /W4 /utf-8 /permissive-)
endif()

enable_testing()
add_test(NAME tests COMMAND tests)
