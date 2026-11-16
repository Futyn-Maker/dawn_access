[Download the latest version (dawn-access-windows.zip)](https://github.com/Futyn-Maker/dawn_access/releases/latest/download/dawn-access-windows.zip)

# Dawn Access

Dawn Access is a mod that makes [Until Dawn](https://store.steampowered.com/app/2172010/Until_Dawn/), the remake for PC, playable without sight. It reads the game through your screen reader and adds sounds where the game relies on the picture: choices, quick-time events, Don't Move, exploration and aiming.

## Features

- Speech for all menus, settings and other interface elements
- Status & Collectibles: clues, totems, the butterfly effect, traits and relationships
- Subtitles and closed captions, with the speaker's name when the game shows it
- The dialogue history, line by line
- Chapter titles, character cards, the cards of status updates, the butterfly effect, clues and totems, hints and loading
- The current objective whenever it changes, which the game itself shows only on the status page
- Button prompts with the key the game shows: for the keyboard, for Xbox and PlayStation controllers, and for the keys you have bound
- Choices, timed choices, and the share of players who chose each option
- Quick-time events, and the short moments when a scene asks for a key
- Don't Move in both versions: steering with the keys or a stick, and holding the controller still
- Exploration: a list of what you can reach on foot, room names, a sound beacon and automatic walking
- Aiming, with a key that puts the aim on the target
- Totems: guidance to the premonition and a description of every vision
- Telescopes, the rifle scope, the selfie and items turned in the hands, with a sound that leads the view
- Climbs, ladders, beams and page turns, with a tone for each step
- Notes, letters and other documents, page by page
- Descriptions of pictures and of things that show no text: photos, Dr. Hill's card, Sam's sketchbook, maps, posters, monitors and more
- Descriptions of films: the totem visions, the flashbacks, the opening film, the films on screens in the game, the events of the past and the deaths shown in the epilogue, with descriptions of their own for the cuts of Extreme Content Censoring
- Achievements as they unlock, in the game's language
- The credits, and the survivors' police interviews in the epilogue
- The examples of the QTE settings, played and voiced on a key
- Keyboard and gamepad keys for all mod functions: Xbox controllers, DualSense and DualShock 4
- NVDA, JAWS, Narrator and other screen readers, SAPI and braille

The mod works with all 20 languages of the game and speaks in the language of the game's text.

## Installation

1. Download [dawn-access-windows.zip](https://github.com/Futyn-Maker/dawn_access/releases/latest/download/dawn-access-windows.zip).
2. Unpack it into the game folder, the one that contains the `Windows` folder. For Steam, this is `C:\Program Files (x86)\Steam\steamapps\common\Until Dawn`. If the game is in another Steam library, right-click it in Steam and choose Manage > Browse local files.
3. Start the game. Soon after, while the game is still loading, the mod says that it is loaded.

If the game shows "Failed to load UE4SS.dll" at start, install the [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).

On its first start, the game prepares its shaders, which can take several minutes. Then it asks a few questions. When it asks about subtitles, choose All or Speech: the mod reads subtitles only when the game shows them. If it asks whether to keep its screen reader enabled, choose No, so that the menus are not read twice.

## How to Play

Use the arrow keys or the D-pad to move through menus, Enter or A to select, and Backspace or B to go back. Left and right change a setting. Q and E, or LB and RB, switch tabs. In the game, Escape or Start pauses it, and Tab or RB opens Status & Collectibles. The mod reads the keys each screen offers.

Controller buttons are named here as on an Xbox controller. On a PlayStation controller, A is Cross, B is Circle, X is Square, Y is Triangle, LB and RB are L1 and R1, LT and RT are L2 and R2, the stick clicks are L3 and R3, Start is Options, and Back is a press of the touchpad.

The arrow keys, Backspace, Escape, Enter and Space, or the D-pad, A, B and Start, also stop the speech as you press them, so a list moves on at once and a screen you leave is not still being read over the one you arrive at. In menus, Q and E, or LB and RB, stop it too.

The game's settings change what a mechanic asks of you, and the mod follows them. In the Accessibility tab, QTE Behavior can stop the game when the time of a quick-time event runs out, or pass quick-time events by itself, and QTE Input can ask for one button in all of them. Auto-Succeed Don't Move passes Don't Move, and Aim Assist helps you aim. In the Controls tab, Don't Move Type picks the version of Don't Move, and the keys can be bound anew. Controller Icon Type and Mouse/Keyboard Icons decide which keys the game's prompts show, and the mod names the same keys. In the General tab, Subtitles decides which subtitles the game shows, and Show Name whether they name the speaker.

### Mod Keys

| Keyboard | Xbox controller          | PlayStation controller | Action                                           |
| -------- | ------------------------ | ---------------------- | ------------------------------------------------ |
| F5       | Back + A                 | Touchpad + Cross       | Repeat the last message                          |
| F6       | Back + X                 | Touchpad + Square      | Read the current screen                          |
| F7       | Back + B                 | Touchpad + Circle      | Stop speech                                      |
| F8       | Back + Y                 | Touchpad + Triangle    | Help for the current situation                   |
| F9       | Back + RT                | Touchpad + R2          | Turn subtitle reading on or off                  |
| F4       | Back + RB                | Touchpad + R1          | Repeat the last subtitle                         |
| F3       | Back + right stick click | Touchpad + R3          | Switch speech between the screen reader and SAPI |
| F2       | Back + left stick click  | Touchpad + L3          | Switch the verbosity level                       |
| F1       | Back + Start             | Touchpad + Options     | Turn the mod off and on                          |

On an Xbox controller, hold Back (View) and press the second button. On a PlayStation controller, press the touchpad, keep it pressed down and press the second button. The game does not see buttons pressed while Back or the touchpad is held.

F9, F3 and F2 keep their choice for the next start. F9 turns off only the reading of subtitles: words a film shows on the screen, such as a place and a time, are read either way.

F1, or Back + Start, turns the mod off. It then says nothing and plays no sound, and its keys for walking and aiming go back to the game. The same key turns it on again, and it reads the current screen. The mod always starts switched on.

These keys work while you walk, aim or look around:

| Keyboard | Xbox controller | PlayStation controller | Action                                                           |
| -------- | --------------- | ---------------------- | ---------------------------------------------------------------- |
| N        | D-pad right     | D-pad right            | Next target                                                      |
| P        | D-pad left      | D-pad left             | Previous target                                                  |
| H        | D-pad up        | D-pad up               | Where the target is                                              |
| G        | X               | Square                 | Walk to the target, press again to stop; while aiming, aim at it |
| T        | D-pad down      | D-pad down             | Beacon, aim sound or look sound on or off                        |

Some of these keys do more on a few screens, as described below: the dialogue history, the events of the past and the credits.

All keys can be changed in `Windows\Bates\Binaries\Win64\ue4ss\Mods\DawnAccess\DawnAccess.ini` in the game folder. The same file has the volume of the mod's sounds and other options, each described there. Do not give the mod F10, F11 or F12: F10 freezes the game until you press Escape, F11 switches between full screen and a window, and F12 takes a Steam screenshot.

### Verbosity Level

The mod has two verbosity levels, detailed and brief. F2, or Back and the left stick click, switches between them and says which one is now on. The level is kept for the next start.

Everything the game itself shows is read at either level: menus, prompts, subtitles, cards, and what is locked. Brief leaves out only what the mod adds of its own: the place of an item in a list, the kind of a control, such as "slider" or "tab", which option a choice was settled on, whether a quick-time event, Don't Move, a shot or another moment was passed, "reached" when a walk ends at its target, and the words that go with a sound you already hear, such as "Choice", "Timed choice", "Timer", "Time is running out", "Fire!", "Lamp on", "Now" and "Time's up". The sounds stay.

### Choices

The mod says "Choice", or "Timed choice" when the choice has a time limit, which a tick-tock marks as well. Then it reads each option with its side and its key. Hold the key of an option until a rising two-note tone plays: by default, A on the keyboard or LT on a controller for the left option, and D or RT for the right one. With Choice Input Type set to Legacy in the Controls tab, the right stick chooses instead. The mod names the key the game shows beside each option, and says the option as you start to hold it. If you let go too early, the choice starts over.

When the time runs out, a falling tone plays and the mod says "Time's up". In some scenes, letting the time run out is an answer of its own, and it sounds the same.

Once you have finished the game, the General tab has Global Stats. With it on, the mod says after each option the share of players who chose it.

### Prompts

When the game shows a button prompt, the mod reads it with its key. Press the key to act.

- A prompt with a time limit starts with a tick-tock, and its key is said at once.
- Some prompts last only a moment, such as catching a swing. The mod plays the direction tone of a quick-time event and says the key at once. A rising tone follows when you press in time, and a falling one when the moment passes.
- When a move is shown together with a key to hold, the mod says "Hold" with both, for example "Hold: RT, left stick right". Press the key first and keep holding it, then make the move.
- Climbs, ladders, shimmies and some pages go step by step. Each step sounds a tone, higher as you go on and lower as you go back. "Onward" and "Back" name the keys for the next and the previous step.
- Some interactions ask for a half circle with the left stick. Clockwise, push the stick all the way up, then right, then down; anticlockwise, up, then left, then down; each step within half a second of the last. On the keyboard, the key in the prompt does it at once.

### Quick-Time Events

The mod says the key and plays the tone of its direction: three rising notes for up, three falling notes for down, and two notes in the left or the right ear for the sides. On the keyboard, the keys are W for up, S for down, A for left and D for right. On a controller, the direction is where the button sits: Y up, A down, X left and B right. Some events ask for a push of the left stick; on the keyboard, move the mouse that way. A wrong button fails the event.

The tone plays again when the game starts to accept the press, and a press before that is ignored. While the time runs, the key is said again every second. A rising two-note tone follows a success, and a falling one a failure or the time running out.

The game's QTE Behavior can stop the game when the time runs out, until you press the key, or pass the events by itself. QTE Input set to Single Icon asks for the same button in every event, the one chosen in Single QTE Input. In the Accessibility tab, on the QTE rows, G or the right stick click plays the example the game shows for the setting, voiced as in play. It stops when you leave the row or change its value.

### Don't Move

At tense moments the game says "Don't Move!", in one of two versions. Don't Move Type in the Controls tab picks it. With Dynamic, the default, the version follows the device you used last before the moment begins: a PlayStation controller gives the passive version, and the keyboard or another controller the active one. Choose Active or Passive to keep one version. Auto-Succeed Don't Move in the Accessibility tab passes them all.

In the active version, the game pushes a marker out of a zone in bursts, and you push it back with the movement keys or the left stick. The mod says "Stay calm!" as it begins, and its sounds tell you what to do:

- The double ping, every half second, means the marker is inside and nothing is asked.
- Two equal notes in one ear mean tap that way, now.
- Three rising notes mean tap up, and three falling notes tap down.
- A low thud before the signal means the marker is already past the edge. The signal then points back to the middle.

Answer each signal with one brief tap, like a click, and listen. The double ping means the marker is back. The same signal again means tap once more, and a signal from the other side means you went too far. A held key crosses the whole zone in a tenth of a second. A burst answered within about three quarters of a second costs nothing. After that, every moment past the edge drains a pool that never refills, and a tick-tock sounds when half and three quarters of it are gone. Key Sensitivity and Stick Sensitivity in the Controls tab make the taps gentler.

In the passive version, hold the controller still. The game then watches only whether the controller moves, not where the marker is. A low thud five times a second means it sees the controller move, and the double ping every half second means it is still. After a short grace at the start, every moment of movement drains a pool that never refills, and the tick-tock marks half and three quarters of it.

### Exploration

Walk with WASD or the left stick; the game has no key to run. The mouse, the arrow keys or the right stick turn the camera. When the game offers an interaction, the mod reads it with its key, usually E, the left mouse button or A. Backspace, the right mouse button or B backs out of it. A soft rising tone plays when you can walk, and a falling one when you no longer can.

When you get control of a character, the mod names the nearest place to go. N and P, or D-pad right and left, choose another one. F6, or Back + X, names the room you are in and the nearest places with their distance and direction.

The list holds what you can reach on foot from where you are without opening a door or climbing, up to 200 metres away. After a door or a climb, it changes. "Way on" is where the scene continues. Places the game opens up later are said with "New". When you walk into another room, the mod says its name. Distances are measured along the way you would walk, and directions are relative to the camera. "Higher up" and "lower down" mean another floor.

You can get to the chosen place in two ways:

- Press G, or X on the controller, to walk there automatically. Press it again or move to stop. If the way is blocked, the mod says "Could not get there", or who is in the way.
- Follow the beacon. It sounds on the side of the next turn, rises as you get closer and drops an octave when the place is behind you. H or D-pad up says the distance and direction, and T or D-pad down turns the beacon off and on.

Some things can be used only while the character or the camera looks at them. Then the mod says "Turn toward it" or "Look at it", and the look sound leads you: a blip on the side to turn to, higher or lower, and a double ping when you face it. To turn the character on the spot, tap a movement key toward the sound; on a controller, push the left stick lightly, less than halfway, and let go within a second. Turn the camera with the mouse, the arrow keys or the right stick. While the look sound leads, T or D-pad down turns it off and on instead of the beacon.

On a narrow beam, the mod says "On a beam". The stick then moves the character only the way they face, and pushing back turns them round. The beacon leads along the beam, and G or X walks along it.

Many things have no name in the game, so the mod reads the names its makers gave them. They are in English and are sometimes run together, such as "pinuppostcards", but in most cases they still make clear what the thing is.

### Totems

A totem is in the list like any other place. Pick it up with the key the game names. The look sound then leads you to the premonition on the totem: turn the totem toward the sound with the left stick or the movement keys. A double ping means you look at it: hold still until the vision begins. The mod describes the vision as it plays. For a totem you have already found, the mod says "Already found".

In the Totems tab of Status & Collectibles, the page of a found totem plays its vision again, with its description. F or Y there plays the events of the past, a film that becomes clear as you find totems. N or D-pad right says what the part on the screen shows, and the parts of the totems not found yet show only static.

### Looking and Turning

Some scenes ask you to look through a telescope or a rifle scope, or to frame a selfie. A sound leads the view: a blip on the side of the point to look at, higher or lower, quicker the nearer it is, and a double ping when the view rests on it. Turn the view with the mouse, the arrow keys or the right stick. H or D-pad up says where the point is, and T or D-pad down turns the sound off and on. Then press the key the game names, if it names one.

Some items show something when you turn them in your hands. The sound leads the turn in the same way: on the right or the left, higher or lower for a turn up or down. A double ping means the item has shown it. Turn it with the movement keys or the left stick. H or D-pad up says which way to turn and how much is left.

### Aiming

Aim with the mouse, the movement keys or the right stick. A blip sounds on the side of the target. It is higher when the target is above the aim and lower when it is below, and it gets faster as the aim gets closer. A double ping means that a shot would hit now, and the mod says "Fire!" when the aim stays on. Shoot with the left mouse button, Space or RT. A shot without the double ping does nothing. On a DualSense with the game's adaptive triggers on, the shot counts only once R2 is pulled past its resistance. A tick-tock at the start means a time limit, and a falling tone means it ran out.

N and P, or D-pad right and left, switch between the targets. H or D-pad up says where the target is, and T or D-pad down turns the aim sound off and on. The targets are named by the names the game's makers gave them, in English.

G, or X on the controller, puts the aim on the target and holds it there, a moving one too, until you aim yourself. It does not shoot: press the shoot key for that. It does not work where only the aim is asked, where the targets are hidden, nor where the aim chooses between two people. With Aim Assist on in the Accessibility tab, the game also pulls the aim onto a target near it when you let go of the stick.

Where the aim chooses between two people, the mod says who is on each side. The sound plays from the side the aim points to, and a double ping means that side is chosen. Then the shoot key fires. Doing nothing lets the time run out, and that is a choice too.

### Notes and Documents

Notes, letters and other documents are read when the game shows their text. With Transcript Display set to Default On in the Accessibility tab, the text shows at once. Otherwise press E, or X on the controller, to show it. Turn the pages with the key the game names: each page is read as you turn to it, and a clue's card is read before its page. Backspace, the right mouse button or B puts the document away.

### Special Moments

A few scenes ask for something of their own. The mod reads them as they come, and F8 explains each one there.

- The boiler in chapter 2: the double ping sounds when its pressure lamp comes on. Press the key the game names at once: the lamp stays lit for about two seconds. A press while it is dark fails, and it all begins again. After the turn, let go of the grab key: a key still held counts at once, before the lamp is lit.
- The phone in the prologue unlocks after several quick swipes. Repeat the swipe again and again: each one moves the slider on a little, and it slides back a moment after the last one. The blips rise as it fills and fall as it slides back.
- The trap in chapter 5 can be sprung only at one moment, which the double ping marks: press the grab key then.
- The lever in chapter 4: once you hold it, the mod says who is on each side.
- The radio in chapter 6: the mod says the position of the dial as it turns, and "Onward" names the way to the station.
- The snow globe in chapter 7 can be shaken, which the game shows no prompt for. F8 near it says how.

### Dialogue History

Dialogue History in the pause menu holds the recent lines, newest first. The mod reads them one at a time: N or D-pad down goes to the next, older line, and P or D-pad up back to a newer one.

### Credits

During the credits at the end of the game, N or D-pad right reads the credits that have appeared since you last pressed it; the first press reads them all from the start. The mod names the key as the credits begin. The survivors' police interviews are read as they come, with the room and the time on the tape.

## Reporting Problems

[Open an issue](https://github.com/Futyn-Maker/dawn_access/issues) and attach the log, `Windows\Bates\Binaries\Win64\ue4ss\Mods\DawnAccess\DawnAccess.log` in the game folder, and `DawnAccess.log.old` from the same folder if it is there. Zip the log if it is too large to attach. If something on screen is not read, press Ctrl+F9, or hold Back and press LB, while it is shown and attach the file it saves in the `dumps` folder next to the log. If asked for a more detailed log, press Ctrl+F11, or hold Back and press LT: it switches the log level and says which one is on, and the choice is kept for the next start.

If the game crashed, attach the crash dump as well, zipped. Where it is depends on how the crash looked:

- A window titled "Fatal Error!" names the dump, a `crash_` file in `Windows\Bates\Binaries\Win64\ue4ss` in the game folder. Attach that file.
- If the game closed by itself with no such window, attach the newest `.dmp` file in `Windows\Bates\Binaries\Win64\.crashdata\reports` in the game folder.

## Development

### Prerequisites

1. Visual Studio 2022 Build Tools with the C++ workload, which includes CMake and Ninja:

    ```
    winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended"
    ```

    Visual Studio 2022 version 17.13 or newer with "Desktop development with C++" works too.

2. Rust:

    ```
    winget install --id Rustlang.Rustup
    ```

3. [Git for Windows](https://git-scm.com).

4. A GitHub account linked to an Epic Games account. UE4SS uses a private repository of Epic Games. Link the accounts on the [Connections page](https://www.epicgames.com/account/connections) of your Epic Games account, then accept the invitation to the EpicGames organization that GitHub sends by email. Git asks you to sign in to GitHub the first time it downloads that repository.

### Building from Source

Clone the repository into a short path. It must be 78 characters or shorter, because UE4SS has long file names. Then run the build:

```
git clone https://github.com/Futyn-Maker/dawn_access.git
cd dawn_access
scripts\package.cmd
```

The first run downloads the dependencies and builds everything, which takes several minutes. Later runs rebuild only what changed. Before it lays the release out, the script checks the language tables and runs the tests, and it stops if they fail.

The result is in `dist\DawnAccess-<version>`. Copy its contents into the game folder, as in the installation. `dist\dawn-access-windows.zip` holds the same files as the release archive, and `dist\DawnAccess-<version>-symbols` the debug symbols of the same build.

Before a pull request, run `scripts\format.cmd` to format the C++ code. It needs the "C++ Clang tools for Windows" component of Visual Studio.

### Tests

The parts of the mod that do not touch the game have tests in `tests`: the rules each feature decides by, such as when a choice is read or where a walk can go, many of them checked against moments recorded in play, as well as the settings file, the language tables and the key table. Run them from the repository folder before a pull request:

```
scripts\test.cmd
```

The first run downloads the doctest framework and builds the tests into `build\tests`.

### Translation

The mod's own messages are in `mod\lang`, one file per game language: `ru_RU.ini`, `de_DE.ini` and so on. `en_US.ini` is the reference. The folders under it hold the descriptions in the same way: `totems` for the totem visions, `films` for the films, `pictures` for pictures and things, and `rooms` for the room names.

Each line is `key=value`. Translate the value and keep the key. Keep `{0}`, `{1}` and so on: the mod puts names and keys there. Leave "Dawn Access" as it is, and keep words written on things, such as signs, in English, as the game shows them. A value that starts with `=` takes the words of another entry, and one that starts with `@` names a text of the game's: keep both as they are.

To try a change without building, edit the same file in the game folder, in `Windows\Bates\Binaries\Win64\ue4ss\Mods\DawnAccess\lang`, and restart the game. The mod uses the file of the game's text language. `Language` in `DawnAccess.ini` can choose another one.

Before a pull request, check from the repository folder that every file has the same keys as the English one:

```
powershell -ExecutionPolicy Bypass -File scripts\check-lang.ps1
```

## License

MIT. The mod is built on [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) (MIT) and uses [Prism](https://github.com/ethindp/prism) (MPL-2.0) for speech. Their licenses are included in the release.
