#pragma once
// The unread-text audit: every text the game draws outside its menu pages is checked, a few
// seconds after it appears, against what the mod has said lately; a text nothing said goes
// to the log as "unread: <class> "<text>"", once while it stays. The audit never speaks: it
// is the instrument that shows, from the log alone, where the game put words on the screen
// that no feature read, before a player has to notice the silence.

namespace da::diag
{
    void InstallUnreadAudit();
}
