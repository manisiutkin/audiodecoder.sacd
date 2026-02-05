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
  kodi::Log(ADDON_LOG_INFO, "Unload SACD Audio Decoder plugin", "DSD2PCM");
}

bool CSACDAudioDecoder::SupportsFile(const std::string& filename)
{
  int track = 0;
  const std::string toLoad = GetTrackName(filename, track);
  return sacd_disc_t::g_is_sacd(toLoad);
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
  m_setting_volAdjust = CSACDSettings::GetInstance().GetVolumeAdjust();
  m_setting_lfeAdjust = CSACDSettings::GetInstance().GetLFEAdjust();
  m_setting_outSamplerate = CSACDSettings::GetInstance().Samplerate();

  /*
   * Start load and init of stream
   */
  if (!sacd_disc_t::g_is_sacd(toLoad) || !open(toLoad))
    return false;

  uint32_t subSong = GetSubsong(track);
  if (!sacd_reader->select_track(subSong))
  {
    return false;
  }

  memset(m_sacdBitrate, 0, sizeof(m_sacdBitrate));
  m_sacdBitrateIdx = 0;
  m_sacdBitrateSum = 0;

  m_dsdOutput = (m_setting_outputType == output_type_e::DSD);
  m_dsdSamplerate = sacd_reader->get_samplerate(subSong);
  m_framerate = sacd_reader->get_framerate(subSong);
  m_channels = sacd_reader->get_channels(subSong);
  int spkConfig = sacd_reader->get_loudspeaker_config(subSong);
  m_outChannelMap = GetSACDChannelMapFromLoudspeakerConfig(spkConfig);
  if (m_outChannelMap.empty())
  {
    m_outChannelMap = GetSACDChannelMapFromChannels(m_channels);
  }

  if (m_dsdOutput)
  {
    kodi::Log(ADDON_LOG_INFO, "Produce DSD stream", "DSD2PCM");
  }
  else
  {
    m_pcmMinSamplerate = 44100;
    while ((m_pcmMinSamplerate / m_framerate) * m_framerate != m_pcmMinSamplerate)
    {
      m_pcmMinSamplerate *= 2;
    }

    m_outSamplerate = std::max(m_pcmMinSamplerate, m_setting_outSamplerate);
    m_pcmOutMaxSamples = m_outSamplerate / m_framerate;
    m_pcmBuffer.resize(m_channels * m_pcmOutMaxSamples);

    double* fir_data = nullptr;
    int fir_size = 0;
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
  }

  m_dstDecoder_initialized = false;
  m_readFrame = true;

    /*
   * Set values for Kodi
   */
  channels = m_channels;
  bitrate = (int64_t)(m_dsdSamplerate * m_channels) + 500;
  totaltime = sacd_reader->get_duration(subSong) * 1000;
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
  uint8_t* dsd_data = nullptr;
  size_t dsd_size = 0;
  while (m_readFrame)
  {
    m_dsxBuf.resize(m_dsdSamplerate / 8 / m_framerate * m_channels);
    uint8_t* frame_data = m_dsxBuf.data();
    size_t frame_size = m_dsxBuf.size();
    frame_type_e frame_type;
    m_readFrame = sacd_reader->read_frame(frame_data, &frame_size, &frame_type);
    if (m_readFrame)
    {
      switch (frame_type)
      {
        case frame_type_e::DSD:
          dsd_data = frame_data;
          dsd_size = frame_size;
          break;
        case frame_type_e::DST:
          if (!m_dstDecoder_initialized)
          {
            if (m_dstDecoder.init(m_channels, m_dsdSamplerate / 8 / m_framerate) != 0)
            {
              return AUDIODECODER_READ_ERROR;
            }
            m_dstDecoder_initialized = true;
          }
          if (m_dstDecoder_initialized)
          {
            m_dsxBuf.resize(frame_size);
            m_dstDecoder.run(m_dsxBuf);
          }
          dsd_data = m_dsxBuf.data();
          dsd_size = m_dsxBuf.size();
          break;
        default:
          return AUDIODECODER_READ_ERROR;
      }
      m_sacdBitrateIdx = (++m_sacdBitrateIdx) % BITRATE_AVGS;
      m_sacdBitrateSum -= m_sacdBitrate[m_sacdBitrateIdx];
      m_sacdBitrate[m_sacdBitrateIdx] = (int64_t)8 * frame_size * m_framerate;
      m_sacdBitrateSum += m_sacdBitrate[m_sacdBitrateIdx];
    }
    if (dsd_size)
    {
      break;
    }
  }
  if (!dsd_size)
  {
    if (m_dstDecoder_initialized)
    {
      m_dsxBuf.clear();
      m_dstDecoder.run(m_dsxBuf);
    }
  }

  if (dsd_size)
  {
    if (m_dsdOutput)
    {
      /*
       * Output now the processed data as the DSD stream and give Kodi.
       */
      uint8_t* currentPtr = dsd_data;

      actualsize = dsd_size;
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
       * Convert now the processed data to needed PCM format and give Kodi.
       */
      auto pcm_out_samples =
          m_dsdpcmDecoder.convert(dsd_data, dsd_size, m_pcmBuffer.data()) / m_channels;
      AdjustVolume(m_pcmBuffer.data(), pcm_out_samples, m_channels);
      AdjustLFE(m_pcmBuffer.data(), pcm_out_samples, m_channels, m_outChannelMap);

      float* currentPtr = m_pcmBuffer.data();

      actualsize = pcm_out_samples * m_channels * sizeof(float);
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
  if (!sacd_reader->seek(seconds))
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

  if (!sacd_disc_t::g_is_sacd(toLoad) || !open(toLoad))
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
    sacd_reader->get_info(subSong, tagInternal);
    tag.SetAlbum(tagInternal.GetAlbum());
    tag.SetAlbumArtist(tagInternal.GetAlbumArtist());
    tag.SetDisc(tagInternal.GetDisc());
    tag.SetReleaseDate(tagInternal.GetReleaseDate());
    return true;
  }

  uint32_t subSong = GetSubsong(track);
  sacd_reader->get_info(subSong, tag);

  tag.SetDuration(sacd_reader->get_duration(subSong));
  tag.SetChannels(sacd_reader->get_channels(subSong));
  tag.SetBitrate(
      ((int64_t)(sacd_reader->get_samplerate(subSong) * sacd_reader->get_channels(subSong)) + 500));
  tag.SetSamplerate(sacd_reader->get_samplerate());

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

uint32_t CSACDAudioDecoder::GetSubsongCount(bool forceOtherIfEmpty)
{
  uint32_t track_count = 0;
  switch (CSACDSettings::GetInstance().GetSpeakerArea())
  {
    case AREA_TWOCH:
      track_count = sacd_reader->get_track_count(AREA_TWOCH);
      if (track_count == 0 && forceOtherIfEmpty)
      {
        sacd_reader->set_mode(access_mode | AREA_MULCH);
        track_count = sacd_reader->get_track_count(AREA_MULCH);
      }
      break;
    case AREA_MULCH:
      track_count = sacd_reader->get_track_count(AREA_MULCH);
      if (track_count == 0 && forceOtherIfEmpty)
      {
        sacd_reader->set_mode(access_mode | AREA_TWOCH);
        track_count = sacd_reader->get_track_count(AREA_TWOCH);
      }
      break;
    default:
      track_count =
          sacd_reader->get_track_count(AREA_TWOCH) + sacd_reader->get_track_count(AREA_MULCH);
      break;
  }

  return track_count;
}

uint32_t CSACDAudioDecoder::GetSubsong(uint32_t p_index)
{
  return sacd_reader->get_track_number(p_index);
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
