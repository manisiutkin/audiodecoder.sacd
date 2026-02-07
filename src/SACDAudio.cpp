/*
 *  Copyright (C) 2020-2021 Team Kodi <https://kodi.tv>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "SACDAudio.h"

#include "Settings.h"

#include <kodi/tools/StringUtils.h>
#include <regex>

namespace
{

std::string getFileExt(const std::string& s)
{
  size_t i = s.rfind('.', s.length());
  if (i != std::string::npos)
  {
    return (s.substr(i + 1, s.length() - i));
  }

  return ("");
}

} // namespace

CSACDAudioDecoder::CSACDAudioDecoder(const kodi::addon::IInstanceInfo& instance)
  : CInstanceAudioDecoder(instance)
{
}

CSACDAudioDecoder::~CSACDAudioDecoder()
{
  m_dsdpcmDecoder.free();
  kodi::Log(ADDON_LOG_INFO, "Unload SACD Audio Decoder plugin", "DSD2PCM");
}

bool CSACDAudioDecoder::SupportsFile(const std::string& filename)
{
  int track = 0;
  const std::string toLoad = GetTrackName(filename, track);
  return udsd_reader_sacd_t::g_is_sacd(toLoad);
}

bool CSACDAudioDecoder::Init(const std::string& filename,
                             unsigned int filecache,
                             int& channels,
                             int& samplerate,
                             int& bitspersample,
                             int64_t& totaltime,
                             int& bitrate,
                             AudioEngineDataFormat& format,
                             std::vector<AudioEngineChannel>& channellist)
{
  /*
   * get the track name from path
   */
  int track = 0;
  std::string toLoad = GetTrackName(filename, track);

  /*
   * Handle settings
   */
  CSACDSettings::GetInstance().Load();

  m_setting_outputType = CSACDSettings::GetInstance().GetOutputType();
  m_setting_outSamplerate = CSACDSettings::GetInstance().GetSamplerate();
  m_setting_volAdjust = CSACDSettings::GetInstance().GetVolumeAdjust();
  m_setting_lfeAdjust = CSACDSettings::GetInstance().GetLFEAdjust();
  m_setting_transition = CSACDSettings::GetInstance().GetTransition();
  m_setting_channelMap = CSACDSettings::GetInstance().GetChannelMap();
  m_setting_decimation = CSACDSettings::GetInstance().GetDecimation();
  m_setting_ramp = CSACDSettings::GetInstance().GetRamp();
  m_setting_logOverload = CSACDSettings::GetInstance().GetLogOverload();

  /*
   * Start load and init of stream
   */
  if (!udsd_reader_sacd_t::g_is_sacd(toLoad) || !open(toLoad))
  {
    return false;
  }

  if (CSACDSettings::GetInstance().GetEditedMaster())
  {
    udsd_reader->set_mode(ACCESS_MODE_EDITED_MASTER_TRACK);
  }

  unsigned subSong = GetSubsong(track);
  if (!udsd_reader->select_track(subSong))
  {
    return false;
  }

  if (!udsd_reader->is_gapless(subSong))
  {
    kodi::Log(ADDON_LOG_WARNING, "Track is not gapless");
  }

  memset(m_sacdBitrate, 0, sizeof(m_sacdBitrate));
  m_sacdBitrateIdx = 0;
  m_sacdBitrateSum = 0;

  m_dsdOutput = (m_setting_outputType == output_type_e::DSD);
  AdjustOutput(subSong);

  if (m_dsdOutput)
  {
    kodi::Log(ADDON_LOG_INFO, "Produce DSD stream", "DSD2PCM");

    for (auto chId : m_setting_channelMap)
    {
      int channel = chId - '0';
      if (channel >= 0 && channel < m_channels)
      {
        m_dsdChannelMap.push_back(channel);
      }
      else
      {
        break;
      }
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_INFO, "Produce PCM stream", "DSD2PCM");

    m_pcmOutMaxSamples = m_outSamplerate / m_framerate;
    m_pcmBuffer.resize(m_channels * m_pcmOutMaxSamples);

    double* fir_data = nullptr;
    size_t fir_size = 0;
    if (CSACDSettings::GetInstance().GetConverterType() == conv_type_e::USER)
    {
      std::string path = CSACDSettings::GetInstance().GetConverterFirFile();
      if (!path.empty() && LoadFir(kodi::addon::GetAddonPath(path)))
      {
        fir_data = m_firData.data();
        fir_size = m_firData.size();
      }
    }
    m_decimation = m_setting_decimation;
    if (!m_decimation)
    {
      m_decimation = (fir_size < 80) ? 8 : 1 << (int)std::log2(fir_size / 10.0);
    }

    m_dsdpcmDecoder.set_payback(false);
    int rv = m_dsdpcmDecoder.init(m_channels, m_framerate, m_dsdSamplerate, m_outSamplerate,
                                  CSACDSettings::GetInstance().GetConverterType(),
                                  CSACDSettings::GetInstance().GetConverterFp64(),
                                  fir_data, fir_size, m_decimation);
    if (rv < 0)
    {
      if (rv == -2)
      {
        kodi::Log(ADDON_LOG_ERROR, "No installed FIR, continue with the default", "DSD2PCM");
      }
      int rv = m_dsdpcmDecoder.init(m_channels, m_framerate, m_dsdSamplerate, m_outSamplerate,
                                    conv_type_e::DIRECT, CSACDSettings::GetInstance().GetConverterFp64());
      if (rv < 0)
      {
        return false;
      }
    }
    
    m_firDelay = float(m_dsdpcmDecoder.get_delay());
    m_outSamplerate = int(m_dsdpcmDecoder.get_decoder_samplerate());
    m_pcmOutMaxSamples = m_outSamplerate / m_framerate;
    m_pcmBuffer.resize(m_channels * m_pcmOutMaxSamples);
    kodi::Log(ADDON_LOG_INFO, "FIR delay: %f samples", m_firDelay);

    m_outAppendSamples = 0;
    m_outRemoveSamples = 0;
    auto pcmOutDelayInsamples = (m_firDelay > 0.0) ? int(m_firDelay + 0.5) : 0;
    m_outAppendSamples = pcmOutDelayInsamples;
    m_outRemoveSamples = pcmOutDelayInsamples;
    auto dsdpcmFirLength = 2 * m_firDelay * m_dsdSamplerate / 8 / m_outSamplerate;
    auto dsdFrameSize = m_dsdSamplerate / 8 / m_framerate;
    auto dsdSpanFrames = (dsdpcmFirLength > 0) ? int(dsdpcmFirLength / dsdFrameSize + 1) : 0;
    udsd_reader->set_track_span(dsdSpanFrames);
  }

  m_dstDecoder_initialized = false;
  m_readFrame = true;

  /*
   * Set values for Kodi
   */
  channels = m_channels;
  bitrate = (int64_t)(m_dsdSamplerate * m_channels) + 500;
  totaltime = (int64_t)(udsd_reader->get_duration(subSong) * 1000);
  channellist = m_outChannelMap;
  if (m_dsdOutput)
  {
    samplerate = m_dsdSamplerate / 8;
    bitspersample = 8;
    format = AUDIOENGINE_FMT_DSD;
  }
  else
  {
    samplerate = m_outSamplerate;
    bitspersample = pcmOutBitsPerSample;
    format = AUDIOENGINE_FMT_FLOAT;
  }

  return true;
}

int CSACDAudioDecoder::ReadPCM(uint8_t* buffer, size_t size, size_t& actualsize)
{
  /*
   * Check for cases where on call before not enough buffer was available and
   * give now the rest.
   */
  if (m_bytesLeft > 0)
  {
    uint8_t* currentPtr = (uint8_t*)m_bytesLeftNextPtr;

    actualsize = m_bytesLeft;
    if (actualsize > size)
    {
      m_bytesLeft = actualsize - size;
      m_bytesLeftNextPtr = currentPtr + size;
      actualsize = size;
    }
    else
    {
      m_bytesLeft = 0;
    }

    memcpy(buffer, currentPtr, actualsize);
    return AUDIODECODER_READ_SUCCESS;
  }

  /*
   * Perform needed decode processing.
   */
  DecodeRun();

  if (m_dsxBuffer.size())
  {
    if (m_dsdOutput)
    {
      if (!m_dsdChannelMap.empty())
      {
        /*
         * Remap DSD channels for weird devices.
         */
        std::vector<uint8_t> frame(m_channels);
        for (auto sample = 0U; sample < m_dsxBuffer.size() / m_channels; ++sample)
        {
          for (auto channel = 0; channel < m_channels; ++channel)
          {
            auto id = (channel < m_dsdChannelMap.size()) ? m_dsdChannelMap[channel] : channel; 
            frame[channel] = m_dsxBuffer[sample * m_channels + id];
          }
          memcpy(&m_dsxBuffer[sample * m_channels], &frame[0], m_channels);
        }
      }
      
      /*
       * Output now the processed data as the DSD stream and give Kodi.
       */
      uint8_t* currentPtr = m_dsxBuffer.data();

      actualsize = m_dsxBuffer.size();
      if (actualsize > size)
      {
        m_bytesLeft = actualsize - size;
        m_bytesLeftNextPtr = currentPtr + size;
        actualsize = size;
      }

      memcpy(buffer, currentPtr, actualsize);
    }
    else
    {
      /*
       * Output now the processed data as the converted PCM stream and give Kodi.
       */
      float* currentPtr = m_pcmBuffer.data() + m_outOffset;

      actualsize = m_outSamples * m_channels * sizeof(float);
      if (actualsize > size)
      {
        m_bytesLeft = actualsize - size;
        m_bytesLeftNextPtr = currentPtr + size / sizeof(float);
        actualsize = size;
      }

      memcpy(buffer, currentPtr, actualsize);
    }
  }
  else
  {
    actualsize = 0;
    return AUDIODECODER_READ_EOF;
  }

  return AUDIODECODER_READ_SUCCESS;
}

int64_t CSACDAudioDecoder::Seek(int64_t time)
{
  double seconds = time / 1000.;
  if (!udsd_reader->seek(seconds))
    return -1;

  return time;
}

bool CSACDAudioDecoder::ReadTag(const std::string& filename, kodi::addon::AudioDecoderInfoTag& tag)
{
  /*
   * get the track name from path
   */
  int track = 0;
  std::string toLoad = GetTrackName(filename, track);

  if (!udsd_reader_sacd_t::g_is_sacd(toLoad) || !open(toLoad))
    return false;

  std::vector<kodi::vfs::CDirEntry> items;
  if (kodi::vfs::GetDirectory(kodi::vfs::GetDirectoryName(toLoad), "", items))
  {
    std::string artworkPath;
    std::string iconUsed;
    int isoCount = 0;
    for (const auto& item : items)
    {
      if (item.IsFolder())
      {
        if (kodi::tools::StringUtils::EqualsNoCase(item.Label(), "Artwork"))
          artworkPath = item.Path();
        continue;
      }

      // Check amount of iso's, if more as one in folder can related image not identified.
      const std::string ext = getFileExt(item.Label());
      if (kodi::tools::StringUtils::EqualsNoCase(ext, "iso") ||
          kodi::tools::StringUtils::EqualsNoCase(ext, "sacd") ||
          kodi::tools::StringUtils::EqualsNoCase(ext, "data"))
      {
        ++isoCount;
        if (isoCount > 1)
        {
          iconUsed = "";
          break;
        }
        continue;
      }

      if (IsUsableIconFile(item, iconUsed))
        break;
      else
        continue;
    }

    if (iconUsed == "" && !artworkPath.empty())
    {
      if (kodi::vfs::GetDirectory(artworkPath, "", items))
      {
        for (const auto& item : items)
        {
          if (item.IsFolder())
            continue;

          if (IsUsableIconFile(item, iconUsed))
            break;
          else
            continue;
        }
      }
    }
    if (!iconUsed.empty())
    {
      tag.SetCoverArtByPath(iconUsed);
    }
  }

  if (toLoad == filename)
  {
    kodi::addon::AudioDecoderInfoTag tagInternal;
    uint32_t subSong = GetSubsong(1);
    udsd_reader->get_info(subSong, tagInternal);
    tag.SetAlbum(tagInternal.GetAlbum());
    tag.SetAlbumArtist(tagInternal.GetAlbumArtist());
    tag.SetDisc(tagInternal.GetDisc());
    tag.SetReleaseDate(tagInternal.GetReleaseDate());
    return true;
  }

  uint32_t subSong = GetSubsong(track);
  udsd_reader->get_info(subSong, tag);

  tag.SetDuration((int)udsd_reader->get_duration(subSong));
  tag.SetChannels(udsd_reader->get_channels(subSong));
  tag.SetBitrate(
      ((int64_t)(udsd_reader->get_samplerate(subSong) * udsd_reader->get_channels(subSong)) + 500));
  tag.SetSamplerate(udsd_reader->get_samplerate());

  return true;
}

int CSACDAudioDecoder::TrackCount(const std::string& filename)
{
  // Ignore calls within already opened track
  int track = 0;
  if (GetTrackName(filename, track) != filename)
    return 0;

  if (!open(filename))
    return 0;

  return GetSubsongCount(CSACDSettings::GetInstance().GetAreaAllowFallback());
}

frame_span_e CSACDAudioDecoder::DecodeTransitionFrame()
{
  m_dsxBuffer.assign(m_dsdSamplerate / 8 / m_framerate * m_channels, DSD_SILENCE_BYTE);
  m_transition -= 1.0f / float(m_framerate);
  return frame_span_e::IN_TRACK;
}

frame_span_e CSACDAudioDecoder::DecodeReadFrame()
{
  auto dsx_span{frame_span_e::NO_TRACK};
  for (;;)
  {
    if (m_readFrame)
    {
      m_dsxBuffer.resize(m_dsdSamplerate / 8 / m_framerate * m_channels);
      auto [readOk, frameSize, frameType, frameSpan] = udsd_reader->read_frame(m_dsxBuffer.data(), m_dsxBuffer.size());
      if (readOk)
      {
        m_spanQueue.push_front(frameSpan);
        m_dsxBuffer.resize(frameSize);
        m_sacdBitrateIdx = (++m_sacdBitrateIdx) % BITRATE_AVGS;
        m_sacdBitrateSum -= m_sacdBitrate[m_sacdBitrateIdx];
        m_sacdBitrate[m_sacdBitrateIdx] = int64_t(8) * frameSize * m_framerate;
        m_sacdBitrateSum += m_sacdBitrate[m_sacdBitrateIdx];
        if (frameType == frame_type_e::DST)
        {
          if (!m_dstDecoder_initialized)
          {
            if (m_dstDecoder.init(m_channels, m_dsdSamplerate / 8 / m_framerate) != 0)
            {
              return dsx_span;
            }
            m_dstDecoder_initialized = true;
          }
        }
      }
      else
      {
        m_readFrame = false;
      }
    }
    if (!m_readFrame)
    {
      m_dsxBuffer.clear();
    }
    if (m_dstDecoder_initialized)
    {
      m_dstDecoder.run(m_dsxBuffer);
    }
    if (!m_dsxBuffer.empty())
    {
      if (!m_spanQueue.empty())
      {
        dsx_span = m_spanQueue.back();
        m_spanQueue.pop_back();
      }
      break;
    }
    if (m_spanQueue.empty())
    {
      break;
    }
  }
  return dsx_span;
}

bool CSACDAudioDecoder::DecodeRun()
{
  bool hasData;
  do
  {
    hasData = false;
    for (;;)
    {
      auto dsx_span{(m_transition > 0) ? DecodeTransitionFrame() : DecodeReadFrame()};
      if (dsx_span == frame_span_e::IN_TRACK)
      {
        if (m_dsdOutput)
        {
          hasData = true;
          break;
        }
      }
      if (dsx_span == frame_span_e::IN_TRACK)
      {
        m_outSamples = (int)m_dsdpcmDecoder.convert(m_dsxBuffer.data(), m_dsxBuffer.size(), m_pcmBuffer.data()) / m_channels;
        if (m_outSamples > m_outRemoveSamples)
        {
          m_outOffset = m_channels * m_outRemoveSamples;
          m_outSamples -= m_outRemoveSamples; 
          m_outRemoveSamples = 0;
          hasData = true;
          break;
        }
        else
        {
          m_outRemoveSamples -= m_outSamples;
        }
      }
      if (dsx_span == frame_span_e::POST_TRACK || dsx_span == frame_span_e::NO_TRACK)
      {
        m_outOffset = 0;
        if (m_outAppendSamples > 0)
        {
          if (dsx_span == frame_span_e::NO_TRACK)
          {
            std::fill(m_dsxBuffer.begin(), m_dsxBuffer.end(), DSD_SILENCE_BYTE);
          }
          m_outSamples = (int)m_dsdpcmDecoder.convert(m_dsxBuffer.data(), m_dsxBuffer.size(), m_pcmBuffer.data()) / m_channels;
          if (m_outSamples > m_outAppendSamples)
          {
            m_outSamples = m_outAppendSamples;
            m_outAppendSamples = 0;
          }
          else
          {
            m_outAppendSamples -= m_outSamples;
          }
          hasData = true;
          break;
        }
      }
      if (dsx_span == frame_span_e::NO_TRACK)
      {
        break;
      }
    }
    if (hasData)
    {
      AdjustVolume(m_pcmBuffer.data() + m_outOffset, m_outSamples, m_channels);
      AdjustLFE(m_pcmBuffer.data() + m_outOffset, m_outSamples, m_channels, m_outChannelMap);
      if (m_setting_logOverload)
      {
        CheckOverload(m_pcmBuffer.data() + m_outOffset, m_outSamples, m_channels);
      }
      m_outTime += (double)m_outSamples / (double)m_outSamplerate;
    }
    else
    {
      m_dsdpcmDecoder.set_ramp(m_transition > 0.0);
    }
  } while (hasData && m_dsxBuffer.empty());
  return hasData;
}

void CSACDAudioDecoder::AdjustOutput(unsigned subSong)
{
  m_dsdSamplerate = udsd_reader->get_samplerate(subSong);
  m_framerate = udsd_reader->get_framerate(subSong);
  m_channels = udsd_reader->get_channels(subSong);
  int spkConfig = udsd_reader->get_loudspeaker_config(subSong);
  m_outChannelMap = GetSACDChannelMapFromLoudspeakerConfig(spkConfig);
  if (m_outChannelMap.empty())
  {
    m_outChannelMap = GetSACDChannelMapFromChannels(m_channels);
  }
  m_outMinSamplerate = 44100;
  while ((m_outMinSamplerate / m_framerate) * m_framerate != m_outMinSamplerate)
  {
    m_outMinSamplerate *= 2;
  }
  m_outSamplerate = m_setting_outSamplerate;
  m_outSamplerate = std::max(m_outMinSamplerate, m_outSamplerate);
  m_outSamplerate = std::min(
    ((m_outSamplerate % 44100) ? (m_dsdSamplerate / 44100) * 48000 : m_dsdSamplerate) / 8,
    m_outSamplerate);
}

void CSACDAudioDecoder::AdjustVolume(float* pcm_data, size_t pcm_samples, unsigned channels)
{
  for (size_t sample = 0; sample < pcm_samples; sample++)
  {
    for (size_t channel = 0; channel < channels; channel++)
    {
      pcm_data[sample * channels + channel] *= m_setting_volAdjust;
    }
  }
}

void CSACDAudioDecoder::AdjustLFE(float* pcm_data,
                                  size_t pcm_samples,
                                  unsigned channels,
                                  const std::vector<AudioEngineChannel>& channel_config)
{
  if ((channels >= 4) &&
      std::any_of(channel_config.begin(), channel_config.end(),
                  [](AudioEngineChannel i) { return i == AUDIOENGINE_CH_LFE; }) &&
      (m_setting_lfeAdjust != 1.0f))
  {
    for (size_t sample = 0; sample < pcm_samples; sample++)
    {
      pcm_data[sample * channels + 3] *= m_setting_lfeAdjust;
    }
  }
}

const char* CSACDAudioDecoder::GetTimeStamp(double t)
{
  static char ts[13];
  auto fraction = int((t - std::floor(t)) * 1000);
  auto second = int(std::floor(t));
  auto minute = second / 60;
  auto hour = minute / 60;
  ts[0] = '0' + (hour / 10) % 6;
  ts[1] = '0' + hour % 10;
  ts[2] = ':';
  ts[3] = '0' + (minute / 10) % 6;
  ts[4] = '0' + minute % 10;
  ts[5] = '.';
  ts[6] = '0' + (second / 10) % 6;
  ts[7] = '0' + second % 10;
  ts[8] = '.';
  ts[9] = '0' + (fraction / 100) % 10;
  ts[10] = '0' + (fraction / 10) % 10;
  ts[11] = '0' + fraction % 10;
  ts[12] = '\0';
  return ts;
}

void CSACDAudioDecoder::CheckOverload(const float* pcm_data, size_t pcm_samples, unsigned channels)
{
  for (auto sample = 0U; sample < pcm_samples; sample++)
  {
    for (auto channel = 0U; channel < channels; channel++)
    {
      audio_sample v = m_setting_volAdjust * pcm_data[sample * channels + channel];
      if ((v > +PCM_OVERLOAD_THRESHOLD) || (v < -PCM_OVERLOAD_THRESHOLD))
      {
        auto overloadTime = m_outTime + (double)(sample) / (double)m_outSamplerate;
        const char* timeStamp = GetTimeStamp(overloadTime);
        kodi::Log(ADDON_LOG_WARNING, "Overload at ch:%d [%s]", channel, timeStamp);
        break;
      }
    }
  }
}

bool CSACDAudioDecoder::LoadFir(const std::string& path)
{
  if (path.empty())
    return false;

  kodi::vfs::CFile file;
  if (!file.OpenFile(path))
    return false;

  m_firData.clear();

  bool fir_name_is_read = false;
  while (1)
  {
    std::string str;
    if (!file.ReadLine(str))
      break;

    if (!str.empty())
    {
      if (str[0] == '#')
      {
        if (str.length() > 1 && !fir_name_is_read)
        {
          m_firName = str.c_str() + 1;
          m_firName = std::regex_replace(m_firName, std::regex("^ +| +$|( ) +"), "$1");

          fir_name_is_read = true;
        }
      }
      else
      {
        m_firData.push_back(atof(str.c_str()));
      }
    }
  }

  return true;
}

std::vector<AudioEngineChannel> CSACDAudioDecoder::GetSACDChannelMapFromLoudspeakerConfig(
    int loudspeaker_config)
{
  std::vector<AudioEngineChannel> sacd_channel_map;
  switch (loudspeaker_config)
  {
    case 0:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR};
      break;
    case 1:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR, AUDIOENGINE_CH_BL,
                          AUDIOENGINE_CH_BR};
      break;
    case 2:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR, AUDIOENGINE_CH_FC,
                          AUDIOENGINE_CH_LFE};
      break;
    case 3:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR, AUDIOENGINE_CH_FC,
                          AUDIOENGINE_CH_BL, AUDIOENGINE_CH_BR};
      break;
    case 4:
      sacd_channel_map = {AUDIOENGINE_CH_FL,  AUDIOENGINE_CH_FR, AUDIOENGINE_CH_FC,
                          AUDIOENGINE_CH_LFE, AUDIOENGINE_CH_BL, AUDIOENGINE_CH_BR};
      break;
    case 5:
      sacd_channel_map = {AUDIOENGINE_CH_FC};
      break;
    case 6:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR, AUDIOENGINE_CH_FC};
      break;
    default:
      break;
  }
  return sacd_channel_map;
}

std::vector<AudioEngineChannel> CSACDAudioDecoder::GetSACDChannelMapFromChannels(int channels)
{
  std::vector<AudioEngineChannel> sacd_channel_map;
  switch (channels)
  {
    case 1:
      sacd_channel_map = {AUDIOENGINE_CH_FC};
      break;
    case 2:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR};
      break;
    case 3:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR, AUDIOENGINE_CH_FC};
      break;
    case 4:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR, AUDIOENGINE_CH_BL,
                          AUDIOENGINE_CH_BR};
      break;
    case 5:
      sacd_channel_map = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR, AUDIOENGINE_CH_FC,
                          AUDIOENGINE_CH_BL, AUDIOENGINE_CH_BR};
      break;
    case 6:
      sacd_channel_map = {AUDIOENGINE_CH_FL,  AUDIOENGINE_CH_FR, AUDIOENGINE_CH_FC,
                          AUDIOENGINE_CH_LFE, AUDIOENGINE_CH_BL, AUDIOENGINE_CH_BR};
      break;
    default:
      break;
  }
  return sacd_channel_map;
}

unsigned CSACDAudioDecoder::GetSubsongCount(bool forceOtherIfEmpty)
{
  unsigned track_count = 0;
  switch (CSACDSettings::GetInstance().GetPlaybackArea())
  {
    case AREA_TWOCH:
      track_count = udsd_reader->get_track_count(AREA_TWOCH);
      if (track_count == 0 && forceOtherIfEmpty)
      {
        udsd_reader->set_mode(access_mode | AREA_MULCH);
        track_count = udsd_reader->get_track_count(AREA_MULCH);
      }
      break;
    case AREA_MULCH:
      track_count = udsd_reader->get_track_count(AREA_MULCH);
      if (track_count == 0 && forceOtherIfEmpty)
      {
        udsd_reader->set_mode(access_mode | AREA_TWOCH);
        track_count = udsd_reader->get_track_count(AREA_TWOCH);
      }
      break;
    default:
      track_count =
          udsd_reader->get_track_count(AREA_TWOCH) + udsd_reader->get_track_count(AREA_MULCH);
      break;
  }

  return track_count;
}

unsigned CSACDAudioDecoder::GetSubsong(unsigned p_index)
{
  return udsd_reader->get_track_number(p_index);
}

std::string CSACDAudioDecoder::GetTrackName(const std::string& file, int& track)
{
  /*
   * get the track name from path
   */
  const std::string toLoad = kodi::addon::CInstanceAudioDecoder::GetTrack("sacd", file, track);
  if (track > 0)
    --track; // Correct track, as the numbers begin with 1.

  return toLoad;
}

bool CSACDAudioDecoder::IsUsableIconFile(const kodi::vfs::CDirEntry& item, std::string& iconUsed)
{
  if (kodi::tools::StringUtils::EqualsNoCase(item.Label(), "folder.jpg"))
  {
    iconUsed = item.Path();
    return true;
  }
  if (iconUsed.empty())
  {
    if (kodi::tools::StringUtils::EqualsNoCase(item.Label(), "front.jpg") ||
        kodi::tools::StringUtils::EqualsNoCase(item.Label(), "icon.png") ||
        kodi::tools::StringUtils::EqualsNoCase(item.Label(), "icon.jpg") ||
        kodi::tools::StringUtils::EqualsNoCase(item.Label(), "thumb.jpg"))
    {
      iconUsed = item.Path();
    }
  }
  return false;
}
