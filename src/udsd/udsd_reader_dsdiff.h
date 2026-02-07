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

#include "udsd_reader.h"
#include "udsd_utility.h"
#include "id3_tagger.h"
#include <vector>

class ATTR_DLL_LOCAL udsd_reader_dsdiff_t : public udsd_reader_t {
	udsd_media_t* m_file;
	uint32_t      m_mode;
	uint32_t      m_version;
	uint32_t      m_samplerate;
	uint16_t      m_channel_count;
	uint16_t      m_loudspeaker_config;
	bool          m_dst_encoded;
	uint64_t      m_frm8_size;
	uint64_t      m_dsti_offset;
	uint64_t      m_dsti_size;
	uint64_t      m_data_offset;
	uint64_t      m_data_size;
	uint16_t      m_framerate;
	uint32_t      m_frame_size;
	uint32_t      m_frame_count;
	tracklist_t   m_tracklist;
	id3_tagger_t  m_id3_tagger;
	uint64_t      m_id3_offset;
	uint32_t      m_track_number;                
	uint64_t      m_track_start;
	uint64_t      m_track_end;
	uint64_t      m_read_start;
	uint64_t      m_read_end;
	uint32_t      m_span_frames;
	uint64_t      m_span_start;
	uint64_t      m_span_end;
	framelist_t   m_framelist;
	uint32_t      m_frame_nr;
public:
	udsd_reader_dsdiff_t();
	~udsd_reader_dsdiff_t();
	uint32_t get_track_count(uint32_t mode);
	uint32_t get_track_number(uint32_t track_index);
	int get_channels(uint32_t track_number);
	int get_loudspeaker_config(uint32_t track_number);
	int get_samplerate(uint32_t track_number);
	int get_framerate(uint32_t track_number);
	double get_duration(uint32_t track_number);
	bool is_dst(uint32_t track_number);
	bool is_gapless(uint32_t track_number);
	void set_mode(uint32_t selector, bool is_set);
	bool open(udsd_media_t* p_file);
	bool close();
	bool select_track(uint32_t track_number);
	bool set_track_span(uint32_t span_frames);
	std::tuple<bool, size_t, frame_type_e, frame_span_e> read_frame(uint8_t* frame_data, size_t frame_size);
	bool seek(double seconds);
	void get_info(uint32_t subsong, kodi::addon::AudioDecoderInfoTag& info);
	void set_info(uint32_t subsong, const kodi::addon::AudioDecoderInfoTag& info);
	void get_albumart(uint32_t albumart_id, std::vector<uint8_t>& albumart_data);
	void set_albumart(uint32_t albumart_id, const std::vector<uint8_t>& albumart_data);
	void commit();
private:
	std::tuple<double, double>get_track_times(uint32_t track_number);
	std::tuple<uint64_t, uint64_t> get_track_span(uint32_t track_number);
	uint64_t get_dsti_from_frame(uint32_t frame_nr);
	uint32_t get_frame_from_file_position(uint64_t file_pos);
	uint64_t get_file_position_from_time(double seconds);
	uint32_t get_track_from_time(double seconds);
	void write_id3_tag(const void* data, size_t size);
};
