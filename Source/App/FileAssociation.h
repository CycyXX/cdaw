#pragma once

#include "../PCH.h"

namespace cdaw::FileAssociation
{
    /** Registers .cdaw files with this exe for the current user (HKCU, no admin rights needed),
        so double-clicking a project opens CDAW and Explorer shows the CDAW icon.

        A build from the build/ folder only claims the association when none exists or the
        registered exe is gone, so a development build doesn't take it from the published one. */
    void registerIfNeeded();
}
