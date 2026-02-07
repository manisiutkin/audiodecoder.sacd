/*
 *  Copyright (C) 2020-2021 Team Kodi <https://kodi.tv>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Settings.h"

#include <math.h>

bool CSACDSettings::Load()
{
  m_outputType = kodi::addon::GetSettingInt("output-type", 0);
  m_samplerate = kodi::addon::GetSettingInt("samplerate", 352800);
  m_volAdjust = pow(10.0f, kodi::addon::GetSettingFloat("volume-adjust", 0.0f) / 20.0f);
  m_lfeAdjust = pow(10.0f, kodi::addon::GetSettingFloat("lfe-adjust", 0.0f) / 20.0f);
  m_transition = kodi::addon::GetSettingFloat("transition", 0.0f);
  m_channelMap = kodi::addon::GetSettingString("channel-map", "");
  m_dsdpcmMode = kodi::addon::GetSettingInt("dsdpcm-mode", 0);
  m_dsdpcmFirFile = kodi::addon::GetSettingString("firconverter", "");
  m_decimation = kodi::addon::GetSettingInt("decimation", 0);
  m_ramp = kodi::addon::GetSettingFloat("ramp", 0.0f);
  m_logOverload = kodi::addon::GetSettingBoolean("log-overload", false);
  m_playbackArea = kodi::addon::GetSettingInt("area", 0);
  m_separateMultichannel = kodi::addon::GetSettingBoolean("separate-multichannel", false);
  m_editedMaster = kodi::addon::GetSettingBoolean("edited-master", false);
  m_areaAllowFallback = kodi::addon::GetSettingBoolean("area-allow-fallback", true);

  return true;
}

bool CSACDSettings::SetSetting(const std::string& settingName,
                               const kodi::addon::CSettingValue& settingValue)
{
  if (settingName == "output-type")
  {
    if (settingValue.GetInt() != m_outputType)
      m_outputType = settingValue.GetInt();
  }
  else if (settingName == "samplerate")
  {
    if (settingValue.GetInt() != m_samplerate)
      m_samplerate = settingValue.GetInt();
  }
  else if (settingName == "volume-adjust")
  {
    if (settingValue.GetFloat() != m_volAdjust)
      m_volAdjust = settingValue.GetFloat();
  }
  else if (settingName == "lfe-adjust")
  {
    if (settingValue.GetFloat() != m_lfeAdjust)
      m_lfeAdjust = settingValue.GetFloat();
  }
  else if (settingName == "transition")
  {
    if (settingValue.GetFloat() != m_transition)
      m_transition = settingValue.GetFloat();
  }
  else if (settingName == "channel-map")
  {
    if (settingValue.GetString() != m_channelMap)
      m_channelMap = settingValue.GetString();
  }
  else if (settingName == "dsdpcm-mode")
  {
    if (settingValue.GetString() != m_dsdpcmFirFile)
      m_dsdpcmFirFile = settingValue.GetString();
  }
  else if (settingName == "firconverter")
  {
    if (settingValue.GetInt() != m_dsdpcmMode)
      m_dsdpcmMode = settingValue.GetInt();
  }
  else if (settingName == "decimation")
  {
    if (settingValue.GetInt() != m_decimation)
      m_decimation = settingValue.GetInt();
  }
  else if (settingName == "ramp")
  {
    if (settingValue.GetFloat() != m_ramp)
      m_ramp = settingValue.GetFloat();
  }
  else if (settingName == "log-overload")
  {
    if (settingValue.GetBoolean() != m_logOverload)
      m_logOverload = settingValue.GetBoolean();
  }
  else if (settingName == "area")
  {
    if (settingValue.GetInt() != m_playbackArea)
      m_playbackArea = settingValue.GetInt();
  }
  else if (settingName == "separate-multichannel")
  {
    if (settingValue.GetBoolean() != m_separateMultichannel)
      m_separateMultichannel = settingValue.GetBoolean();
  }
  else if (settingName == "edited-master")
  {
    if (settingValue.GetBoolean() != m_editedMaster)
      m_editedMaster = settingValue.GetBoolean();
  }

  return true;
}

conv_type_e CSACDSettings::GetConverterType() const
{
  auto conv_type = conv_type_e::MULTISTAGE;
  switch (m_dsdpcmMode)
  {
    case 0:
    case 1:
      conv_type = conv_type_e::MULTISTAGE;
      break;
    case 2:
    case 3:
      conv_type = conv_type_e::DIRECT;
      break;
    case 4:
    case 5:
      conv_type = conv_type_e::USER;
      break;
  }
  return conv_type;
}

bool CSACDSettings::GetConverterFp64() const
{
  auto conv_fp64 = false;
  switch (m_dsdpcmMode)
  {
    case 1:
    case 3:
    case 5:
      conv_fp64 = true;
      break;
  }
  return conv_fp64;
}
