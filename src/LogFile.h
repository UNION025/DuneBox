/***********************************************************************
LogFile.h - send console output to data/DebugFiles when not in a terminal

Launched from Finder there is no terminal, so stdout/stderr (OF logs,
libfreenect's USB messages, calibration output) would be lost. They go to
data/DebugFiles/dunebox.log instead; the previous log is kept as
dunebox-prev.log. rotateIfLarge() caps the pair at about 2 x kMaxBytes, so a
rig left running all day can't fill the disk. No-op on Windows.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

namespace dunebox {
namespace logfile {

void startUnlessTerminal();   // call first thing in main()
void rotateIfLarge();         // call periodically (cheap)

} // namespace logfile
} // namespace dunebox
