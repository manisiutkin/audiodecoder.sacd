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
  int GetSamplerate() const { return m_samplerate; }
  float GetVolumeAdjust() const { return m_volAdjust; }
  float GetLFEAdjust() const { return m_lfeAdjust; }
  float GetTransition() const { return m_transition; }
  std::string GetChannelMap() const { return m_channelMap; }
  int GetConverterMode() const { return m_dsdpcmMode; }
  const std::string& GetConverterFirFile() const { return m_dsdpcmFirFile; }
  conv_type_e GetConverterType() const;
  bool GetConverterFp64() const;
  int GetDecimation() const { return m_decimation; }
  float GetRamp() const { return m_ramp; }
  bool GetLogOverload() const { return m_logOverload; }
  int GetPlaybackArea() const { return m_playbackArea; }
  bool GetFullPlayback() const { return false; } // unused
  bool GetSeparateMultichannel() const { return m_playbackArea == 0 && m_separateMultichannel; }
  bool GetEditedMaster() const { return m_editedMaster; }
  bool GetAreaAllowFallback() const { return m_areaAllowFallback; }

private:
  CSACDSettings() = default;

  int m_outputType = 0;
  int m_samplerate = 352800;
  float m_volAdjust = 0.0f;
  float m_lfeAdjust = 0.0f;
  float m_transition = 0.0f;
  std::string m_channelMap;
  int m_dsdpcmMode = 0;
  std::string m_dsdpcmFirFile;
  int m_decimation = 0;
  float m_ramp = 0.0f;
  bool m_logOverload = false;
  int m_playbackArea = 0;
  bool m_separateMultichannel = false;
  bool m_editedMaster = false;
  bool m_areaAllowFallback = true;
};
