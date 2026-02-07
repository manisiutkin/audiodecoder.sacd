/*
 *  Copyright (C) 2020-2021 Team Kodi <https://kodi.tv>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "../lib/libdstdec/binding/dst_decoder.h"
#include "../lib/libdsdpcm/binding/dsdpcm_decoder.h"

#include "Settings.h"
#include "udsd/udsd_core.h"
#include "udsd/dsdpcm_wrapper.h"

#include <kodi/addon-instance/AudioDecoder.h>

#include <deque>

constexpr int UPDATE_STATS_MS = 500;
constexpr int BITRATE_AVGS = 16;
constexpr float PCM_OVERLOAD_THRESHOLD = 1.0f;
constexpr unsigned char DSD_SILENCE_BYTE = 0x69;

class ATTR_DLL_LOCAL CSACDAudioDecoder : public kodi::addon::CInstanceAudioDecoder,
                                         public udsd_core_t
{
public:
  CSACDAudioDecoder(const kodi::addon::IInstanceInfo& instance);
  virtual ~CSACDAudioDecoder();

  bool SupportsFile(const std::string& filename) override;
  bool Init(const std::string& filename,
            unsigned int filecache,
            int& channels,
            int& samplerate,
            int& bitspersample,
            int64_t& totaltime,
            int& bitrate,
            AudioEngineDataFormat& format,
            std::vector<AudioEngineChannel>& channellist) override;
  int ReadPCM(uint8_t* buffer, size_t size, size_t& actualsize) override;
  int64_t Seek(int64_t time) override;
  bool ReadTag(const std::string& file, kodi::addon::AudioDecoderInfoTag& tag) override;
  int TrackCount(const std::string& file) override;

private:
  std::vector<AudioEngineChannel> GetSACDChannelMapFromLoudspeakerConfig(int loudspeaker_config);
  std::vector<AudioEngineChannel> GetSACDChannelMapFromChannels(int channels);
  unsigned GetSubsongCount(bool forceOtherIfEmpty);
  unsigned GetSubsong(unsigned p_index);
  frame_span_e DecodeTransitionFrame();
  frame_span_e DecodeReadFrame();
  bool DecodeRun();
  void AdjustOutput(unsigned subSong);
  void AdjustVolume(float* pcm_data, size_t pcm_samples, unsigned channels);
  void AdjustLFE(float* pcm_data, size_t pcm_samples, unsigned channels,
                 const std::vector<AudioEngineChannel>& channel_config);
  const char* GetTimeStamp(double t);
  void CheckOverload(const float* pcm_data, size_t pcm_samples, unsigned channels);
  bool LoadFir(const std::string& path);
  std::string GetTrackName(const std::string& file, int& track);
  bool IsUsableIconFile(const kodi::vfs::CDirEntry& item, std::string& iconUsed);

  // Setting values
  output_type_e m_setting_outputType = output_type_e::PCM;
  int m_setting_outSamplerate = 44100;
  float m_setting_volAdjust = 0.0f;
  float m_setting_lfeAdjust = 0.0f;
  float m_setting_transition = 0.0f;
  std::string m_setting_channelMap;
  int m_setting_decimation = 0;
  float m_setting_ramp = 0.0f;
  bool m_setting_logOverload = false;

  // Processing parts
  dst_decoder_t m_dstDecoder;
  bool m_dstDecoder_initialized;
  std::vector<uint8_t> m_dsxBuffer;
  std::deque<frame_span_e> m_spanQueue;

  dsdpcm_wrapper_t m_dsdpcmDecoder;
  float m_transition = 0.0f;
  int m_decimation = 0;
  float m_firDelay = 0.0f;
  int m_outOffset = 0;
  int m_outSamples = 0;
  int m_outAppendSamples = 0;
  int m_outRemoveSamples = 0;
  double m_outTime = 0.0;

  bool m_dsdOutput;
  int m_dsdSamplerate;
  int m_framerate;
  bool m_readFrame;

  std::vector<double> m_firData;
  std::string m_firName;

  // SACD process
  int64_t m_sacdBitrate[BITRATE_AVGS];
  int m_sacdBitrateIdx;
  int64_t m_sacdBitrateSum;

  // DSD/PCM output data
  int m_channels;
  std::vector<unsigned> m_dsdChannelMap;
  std::vector<AudioEngineChannel> m_outChannelMap;
  int m_outSamplerate;
  int pcmOutBitsPerSample = 32;
  int m_pcmOutMaxSamples;
  uint64_t m_pcmOutOffset;
  int m_outMinSamplerate;
  std::vector<float> m_pcmBuffer;

  // Data for next call if before was not enough space in buffer.
  size_t m_bytesLeft = 0;
  void* m_bytesLeftNextPtr = nullptr;
};
