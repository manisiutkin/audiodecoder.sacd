/*
 *  Copyright (C) 2020-2021 Team Kodi <https://kodi.tv>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "../lib/libdsdpcm/binding/dsdpcm_decoder.h"

#include <kodi/General.h>

enum class output_type_e
{
  PCM = 0,
  DSD = 1
};

class CSACDSettings
{
public:
  static CSACDSettings& GetInstance()
  {
    static CSACDSettings settings;
    return settings;
  }

  bool Load();
  bool SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue);

  output_type_e GetOutputType() const { return static_cast<output_type_e>(m_outputType); }
  float GetVolumeAdjust() const { return m_volAdjust; }
  float GetLFEAdjust() const { return m_lfeAdjust; }
  int Samplerate() const { return m_samplerate; }
  int GetConverterMode() const { return m_dsd2pcmMode; }
  const std::string& GetConverterFirFile() const { return m_dsd2pcmFirFile; }
  conv_type_e GetConverterType() const;
  bool GetConverterFp64() const;
  int GetSpeakerArea() const { return m_speakerArea; }
  bool GetFullPlayback() const { return false; } // unused
  bool GetSeparateMultichannel() const { return m_speakerArea == 0 && m_separateMultichannel; }
  bool GetAreaAllowFallback() const { return m_areaAllowFallback; }

private:
  CSACDSettings() = default;

  int m_outputType = 0;
  float m_volAdjust = 0.0f;
  float m_lfeAdjust = 0.0f;
  int m_samplerate = 352800;
  int m_dsd2pcmMode = 0;
  std::string m_dsd2pcmFirFile;
  int m_speakerArea = 0;
  bool m_separateMultichannel = false;
  bool m_areaAllowFallback = true;
};
