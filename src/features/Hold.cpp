#include "features/Hold.hpp"

namespace da::hold
{
    bool Hold::Update(double now, const Frame& f)
    {
        if (f.state == State::Paused)
        {
            m_wasPaused = true;
            m_freeSince = -1.0;
            m_softSince = -1.0;
            m_backSince = -1.0;
            return false;
        }
        const bool fromPause = m_wasPaused;
        m_wasPaused = false;
        if (fromPause && m_control) m_backSince = now;
        // Play begun from a loading or a frozen screen (a pause leaves the last state as it was):
        // the level's first scene has yet to take the character.
        const bool inPlay = f.state == State::Game;
        if (inPlay && !m_inPlay) m_levelStart = true;
        m_inPlay = inPlay;
        if (m_levelStart && f.character && (f.binding == 1 || f.binding == 2)) m_levelStart = false;
        const bool hardFree = f.character && f.state == State::Game && (f.binding == 0 || f.binding == 3) && f.cameraFree && !f.covered;
        // Movement the game ignores where it offers a thing she stands in leaves the press hers.
        const bool moves = f.inputTaken || f.offered;
        // On the way back from a pause the hold stands as it stood while only the camera or a
        // soft reason is in the way (kPauseReturn).
        bool ended = false;
        if (m_backSince >= 0.0)
        {
            const bool free = hardFree && moves && !f.gameWalks && !f.falling && !f.sceneWaits;
            const bool onTheWay = f.character && inPlay && (f.binding == 0 || f.binding == 3) && !f.covered && now - m_backSince < kPauseReturn;
            if (free)
                m_backSince = -1.0;
            else if (onTheWay)
                return false;
            else
            {
                m_backSince = -1.0;
                ended = true;
            }
        }
        bool control = m_control;
        if (ended)
        {
            control = false;
            m_freeSince = -1.0;
            m_softSince = -1.0;
        }
        else if (!hardFree)
        {
            control = false;
            m_freeSince = -1.0;
            m_softSince = -1.0;
        }
        else if (!moves || f.gameWalks || f.falling || f.sceneWaits)
        {
            m_freeSince = -1.0;
            if (m_control)
            {
                if (m_softSince < 0.0) m_softSince = now;
                if (now - m_softSince >= kSoftTakeBack) control = false;
            }
        }
        else
        {
            m_softSince = -1.0;
            if (!m_control)
            {
                if (m_freeSince < 0.0) m_freeSince = now;
                if (now - m_freeSince >= (m_levelStart ? kLevelSettle : kHandOverSettle))
                {
                    control = true;
                    m_levelStart = false;
                }
            }
        }
        if (m_control && !control && (fromPause || ended)) m_takenSilently = true;
        const bool changed = control != m_control;
        m_control = control;
        if (changed)
        {
            m_why = control                            ? Why::None
                    : !f.character                     ? Why::NoCharacter
                    : f.covered                        ? Why::Covered
                    : f.state != State::Game           ? Why::NotInPlay
                    : f.binding != 0 && f.binding != 3 ? Why::Bound
                    : !f.cameraFree                    ? Why::Camera
                    : f.gameWalks                      ? Why::GameWalks
                    : f.falling                        ? Why::Falling
                    : f.sceneWaits                     ? Why::SceneWaits
                                                       : Why::InputIgnored;
        }
        return changed;
    }

    bool Hold::TakeSilentEnd()
    {
        const bool silent = m_takenSilently;
        m_takenSilently = false;
        return silent;
    }
}
