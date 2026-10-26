//
// tests.h
//
// BMA800XL - A bare metal Atari 800XL emulator for Raspberry Pi
// Copyright (C) 2026 aminch <https://github.com/aminch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Modified for BMCoco.
//
// Shared by the direct display driver's PC tests (see test.cpp).
//

#ifndef BMCOCO_TOOLS_PI4KMS_TESTS_H_
#define BMCOCO_TOOLS_PI4KMS_TESTS_H_

extern int g_failed;
void Check(bool ok, const char *name, const char *what);

// test_modes.cpp: EDID, video modes, InfoFrames, PHY settings.
void RunModeTests(void);

#endif
