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

#include "endianess.h"
#include "udsd_reader.h"
#include "scarletbook.h"
#include <list>
#include <tuple>

#define CP_ACP 0

constexpr int MAX_DST_SIZE = 1024 * 64;

typedef struct {
	uint8_t data[MAX_DST_SIZE];
	int     size;
	int     sector_count;
	int     channel_count;
	int     dst_encoded;
	bool    started;
} audio_frame_t;

class ATTR_DLL_LOCAL udsd_reader_sacd_t : public udsd_reader_t {
	udsd_media_t*        m_file;
	uint32_t             m_mode;
	scarletbook_handle_t m_sb;
	uint8_t              m_md5[16];
	uint32_t             m_area_first_lsn;
	uint32_t             m_area_last_lsn;
	uint32_t             m_track_number;
	uint32_t             m_track_first_lsn;
	uint32_t             m_track_length_lsn;
	uint32_t             m_read_first_lsn;
	uint32_t             m_read_length_lsn;
	uint32_t             m_read_next_lsn;
	uint32_t             m_span_frames;
	uint32_t             m_span_first_lsn;
	uint32_t             m_span_length_lsn;
	uint8_t              m_channel_count;
	bool                 m_track_complete;
	bool                 m_dst_encoded;
	audio_sector_t       m_audio_sector;
	audio_frame_t        m_frame;
	int                  m_frame_info_counter;
	int                  m_packet_info_idx;
	uint8_t              m_sector_buffer[SACD_PSN_SIZE];
	uint32_t             m_sector_size;
	int                  m_sector_bad_reads;
	uint8_t*             m_buffer;
	int                  m_buffer_offset;
public:
	static bool g_is_sacd(const std::string& p_path);
	udsd_reader_sacd_t();
	~udsd_reader_sacd_t();
	std::tuple<scarletbook_area_t*, uint32_t> get_area_and_index_from_track(uint32_t track_number);
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
	bool open(udsd_media_t* media_file);
	bool close();
	bool select_track(uint32_t track_number);
	bool set_track_span(uint32_t span_frames);
	std::tuple<bool, size_t, frame_type_e, frame_span_e> read_frame(uint8_t* frame_data, size_t frame_size);
	bool seek(double seconds);
	void get_info(uint32_t track_number, kodi::addon::AudioDecoderInfoTag& info);
	bool read_blocks_raw(uint32_t lb_start, size_t block_count, uint8_t* data);
	void get_md5(uint8_t md5[16]);
	std::string get_md5();

private:
	uint64_t get_size();
	uint64_t get_offset();
	bool read_master_toc();
	bool read_area_toc(int area_idx);
	void free_area(scarletbook_area_t* area);
	uint32_t get_frame_start_count(uint32_t lsn);
	std::tuple<uint32_t, uint32_t> get_track_span();
	std::tuple<scarletbook_area_t*, uint32_t> get_area_and_index_from_lsn(uint32_t lsn);
};
