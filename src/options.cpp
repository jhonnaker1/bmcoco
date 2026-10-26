//
// options.cpp
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

#include "options.h"

#include <fatfs/ff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Options *Options::s_pThis = 0;

Options::Options(void) : m_nSettings(0), m_nCount(0) {
  m_SettingsPath[0] = '\0';
  s_pThis = this;

  CBcmPropertyTags Tags;
  if (!Tags.GetTag(PROPTAG_GET_COMMAND_LINE, &m_TagCommandLine,
                   sizeof m_TagCommandLine)) {
    return;
  }
  if (m_TagCommandLine.Tag.nValueLength >= sizeof m_TagCommandLine.String) {
    return;
  }
  m_TagCommandLine.String[m_TagCommandLine.Tag.nValueLength] = '\0';

  // Split "a=1 b=2 c" in place into name/value pairs. The firmware adds its
  // own options (some without values), which are kept too and are harmless.
  char *p = (char *)m_TagCommandLine.String;
  while (*p != '\0' && m_nCount < OPTIONS_MAX) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    char *name = p;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }
    if (*p != '\0') {
      *p++ = '\0';
    }
    char *value = strchr(name, '=');
    if (value != 0) {
      *value++ = '\0';
    } else {
      value = name + strlen(name);
    }
    m_pName[m_nCount] = name;
    m_pValue[m_nCount] = value;
    m_nCount++;
  }
}

const char *Options::Get(const char *name, const char *default_value) const {
  int i = FindSetting(name);
  if (i >= 0) {
    return m_SettingValue[i];
  }
  // Later options win, so a user's value overrides an earlier default.
  for (int i = m_nCount - 1; i >= 0; i--) {
    if (strcmp(m_pName[i], name) == 0) {
      return m_pValue[i];
    }
  }
  return default_value;
}

int Options::GetInt(const char *name, int default_value) const {
  const char *value = Get(name);
  return value && *value ? atoi(value) : default_value;
}

double Options::GetDouble(const char *name, double default_value) const {
  const char *value = Get(name);
  return value && *value ? strtod(value, 0) : default_value;
}

bool Options::GetBool(const char *name, bool default_value) const {
  const char *value = Get(name);
  if (value == 0 || *value == '\0') {
    return default_value;
  }
  return strcmp(value, "1") == 0 || strcmp(value, "yes") == 0 ||
         strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
}

int Options::FindSetting(const char *name) const {
  for (int i = 0; i < m_nSettings; i++) {
    if (strcmp(m_SettingName[i], name) == 0) {
      return i;
    }
  }
  return -1;
}

static char *Trim(char *s) {
  while (*s == ' ' || *s == '\t') {
    s++;
  }
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                     end[-1] == '\n')) {
    *--end = '\0';
  }
  return s;
}

void Options::LoadSettings(const char *path) {
  strncpy(m_SettingsPath, path, sizeof m_SettingsPath - 1);
  m_SettingsPath[sizeof m_SettingsPath - 1] = '\0';

  FIL file;
  if (f_open(&file, path, FA_READ) != FR_OK) {
    return;
  }
  char line[SETTING_NAME_MAX + SETTING_VALUE_MAX + 8];
  while (f_gets(line, sizeof line, &file) != 0) {
    char *name = Trim(line);
    if (*name == '#' || *name == '\0') {
      continue;
    }
    char *value = strchr(name, '=');
    if (value == 0) {
      continue;
    }
    *value++ = '\0';
    Set(Trim(name), Trim(value));
  }
  f_close(&file);
}

bool Options::Set(const char *name, const char *value) {
  if (strlen(name) >= SETTING_NAME_MAX || strlen(value) >= SETTING_VALUE_MAX) {
    return false;
  }
  int i = FindSetting(name);
  if (i < 0) {
    if (m_nSettings >= SETTINGS_MAX) {
      return false;
    }
    i = m_nSettings++;
    strcpy(m_SettingName[i], name);
  }
  strcpy(m_SettingValue[i], value);
  return true;
}

bool Options::SaveSettings(void) {
  if (m_SettingsPath[0] == '\0') {
    return false;
  }
  FIL file;
  if (f_open(&file, m_SettingsPath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    return false;
  }
  f_puts("# BMCoco settings, written by its menu (F1).\r\n"
         "# These win over the same options in cmdline.txt.\r\n", &file);
  for (int i = 0; i < m_nSettings; i++) {
    f_printf(&file, "%s=%s\r\n", m_SettingName[i], m_SettingValue[i]);
  }
  return f_close(&file) == FR_OK;
}

Options *Options::Get(void) { return s_pThis; }
