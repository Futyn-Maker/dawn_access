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
            return false;
        }
        const bool fromPause = m_wasPaused;
        m_wasPaused = false;
        const bool hardFree = f.character && f.state == State::Game && (f.binding == 0 || f.binding == 3) && f.cameraFree && !f.covered;
        bool control = m_control;
        if (!hardFree)
        {
            control = false;
            m_freeSince = -1.0;
            m_softSince = -1.0;
        }
        else if (!f.inputTaken || f.gameWalks)
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
                if (now - m_freeSince >= kHandOverSettle) control = true;
            }
        }
        if (m_control && !control && fromPause) m_takenSilently = true;
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
