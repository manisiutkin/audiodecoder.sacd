/*
 *  Copyright (C) 2020 Team Kodi <https://kodi.tv>
 *  Copyright (c) 2011-2026 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

/*
 * Code taken SACD Decoder plugin (from foo_input_sacd) by Team Kodi.
 * Original author Maxim V.Anisiutkin.
 */

#pragma once

#include "udsd_media.h"

#include <kodi/addon-instance/AudioDecoder.h>

enum track_e {
  TRACK_SELECTED = -1,
  TRACK_CUESHEET = 0
};

enum access_mode_e {
  ACCESS_MODE_NULL = 0,
  ACCESS_MODE_TWOCH = 1 << 0,
  ACCESS_MODE_MULCH = 1 << 1,
  ACCESS_MODE_SINGLE_TRACK = 1 << 2,
  ACCESS_MODE_EDITED_MASTER_TRACK = 1 << 3
};

enum class media_type_e {
  INVALID = -1,
  ISO = 0,
  DSDIFF = 1,
  DSF = 2,
};

enum class frame_type_e {
  INVALID = -1,
  DSD = 0,
  DST = 1
};

enum class frame_span_e {
  NO_TRACK = 0,
  PRE_TRACK = 1,
  IN_TRACK = 2,
  POST_TRACK = 3
};

enum area_id_e {
  AREA_NULL = 0,
  AREA_TWOCH = 1 << 0,
  AREA_MULCH = 1 << 1,
  AREA_BOTH = AREA_TWOCH | AREA_MULCH
};

class ATTR_DLL_LOCAL udsd_reader_t {
public:
  udsd_reader_t(){};
  virtual ~udsd_reader_t(){};
  virtual uint32_t get_track_count(uint32_t mode = 0) = 0;
  virtual uint32_t get_track_number(uint32_t track_index) = 0;
  virtual int get_channels(uint32_t track_number = TRACK_SELECTED) = 0;
  virtual int get_loudspeaker_config(uint32_t track_number = TRACK_SELECTED) = 0;
  virtual int get_samplerate(uint32_t track_number = TRACK_SELECTED) = 0;
  virtual int get_framerate(uint32_t track_number = TRACK_SELECTED) = 0;
  virtual double get_duration(uint32_t track_number = TRACK_SELECTED) = 0;
  virtual bool is_dst(uint32_t track_number = TRACK_SELECTED) { return false; };
  virtual bool is_gapless(uint32_t track_number = TRACK_SELECTED) { return true; };
  virtual void set_mode(uint32_t selector, bool is_set = true) = 0;
  virtual bool open(udsd_media_t* media) = 0;
  virtual bool close() = 0;
  virtual bool select_track(uint32_t track_number) = 0;
  virtual bool set_track_span(uint32_t span_frames) { return false; };
  virtual std::tuple<bool, size_t, frame_type_e, frame_span_e> read_frame(uint8_t* frame_data, size_t frame_size) = 0;
  virtual bool seek(double seconds) = 0;
  virtual void get_info(uint32_t track_number, kodi::addon::AudioDecoderInfoTag& info) {
    info.SetDuration((int)get_duration(track_number));
    auto dsd_channels = get_channels(track_number);
    info.SetChannels(dsd_channels);
    auto dsd_samplerate = get_samplerate(track_number);
    info.SetSamplerate(dsd_samplerate);
    info.SetBitrate((((int64_t)dsd_samplerate * (int64_t)dsd_channels) + 500) / 1000);
    std::string codec;
    codec = is_dst(track_number) ? "DST" : "DSD";
    if (dsd_samplerate % 48000 == 0) {
      codec += dsd_samplerate / 48000;
      codec += "/48";
    }
    else {
      codec += dsd_samplerate / 44100;
    }
    info.SetMediaType(codec);
  }
  virtual void set_info(uint32_t track_number, const kodi::addon::AudioDecoderInfoTag& info) {}
  virtual void get_albumart(uint32_t albumart_id, std::vector<uint8_t>& albumart_data) {}
  virtual void set_albumart(uint32_t albumart_id, const std::vector<uint8_t>& albumart_data) {}
  virtual void commit() {}
  virtual double get_instant_bitrate() { return 0.0; };
};
