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

#include "udsd_reader_dsf.h"

#include <algorithm>

udsd_reader_dsf_t::udsd_reader_dsf_t() {
	for (int i = 0; i < 256; i++) {
		swap_bits[i] = 0;
		for (int j = 0; j < 8; j++) {
			swap_bits[i] |= ((i >> j) & 1) << (7 - j);
		}
	}
	m_mode = ACCESS_MODE_NULL;
}

udsd_reader_dsf_t::~udsd_reader_dsf_t() {
	close();
}

uint32_t udsd_reader_dsf_t::get_track_count(uint32_t mode) {
	uint32_t track_mode = mode ? mode : m_mode;
	uint32_t track_count = 0;
	if (track_mode & ACCESS_MODE_TWOCH) {
		if (m_channel_count <= 2) {
			track_count += 1;
		}
	}
	if (track_mode & ACCESS_MODE_MULCH) {
		if (m_channel_count > 2) {
			track_count += 1;
		}
	}
	if (track_mode & ACCESS_MODE_SINGLE_TRACK) {
		track_count = std::min(track_count, uint32_t(1));
	}
	return track_count;
}

uint32_t udsd_reader_dsf_t::get_track_number(uint32_t track_index) {
	return track_index;
}

int udsd_reader_dsf_t::get_channels(uint32_t track_number) {
	return m_channel_count;
}

int udsd_reader_dsf_t::get_loudspeaker_config(uint32_t track_number) {
	return m_loudspeaker_config;
}

int udsd_reader_dsf_t::get_samplerate(uint32_t track_number) {
	return m_samplerate;
}

int udsd_reader_dsf_t::get_framerate(uint32_t track_number) {
	return m_framerate;
}

double udsd_reader_dsf_t::get_duration(uint32_t track_number) {
	return m_samplerate > 0 ? (double)m_sample_count / m_samplerate : 0.0;
}

void udsd_reader_dsf_t::set_mode(uint32_t selector, bool is_set) {
	m_mode = (m_mode & ~selector) | (is_set ? selector : (uint32_t)0);
}

bool udsd_reader_dsf_t::open(udsd_media_t* p_file) {
	m_file = p_file;
	Chunk ck;
	FmtDSFChunk fmt;
	int64_t pos;
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck == "DSD ")) {
		return false;
	}
	if (ck.get_size() != hton64((int64_t)28)) {
		return false;
	}
	if (m_file->read(&m_file_size, sizeof(m_file_size)) != sizeof(m_file_size)) {
		return false;
	}
	if (m_file->read(&m_id3_offset, sizeof(m_id3_offset)) != sizeof(m_id3_offset)) {
		return false;
	}
	if (m_id3_offset == 0) {
		m_id3_offset = m_file_size;
	}
	pos = m_file->get_position();
	if (!(m_file->read(&fmt, sizeof(fmt)) == sizeof(fmt) && fmt == "fmt ")) {
		return false;
	}
	if (fmt.format_id != 0) {
		return false;
	}
	m_version = fmt.format_version;
	switch (fmt.channel_type) {
	case 1:
		m_loudspeaker_config = 5;
		break;
	case 2:
		m_loudspeaker_config = 0;
		break;
	case 3:
		m_loudspeaker_config = 6;
		break;
	case 4:
		m_loudspeaker_config = 1;
		break;
	case 5:
		m_loudspeaker_config = 2;
		break;
	case 6:
		m_loudspeaker_config = 3;
		break;
	case 7:
		m_loudspeaker_config = 4;
		break;
	default:
		m_loudspeaker_config = 65535;
		break;
	}
	if (fmt.channel_count < 1) {
		return false;
	}
	m_channel_count = fmt.channel_count;
	m_samplerate = fmt.samplerate;
	m_framerate = 75;
	switch (fmt.bits_per_sample) {
	case 1:
		m_is_lsb = true;
		break;
	case 8:
		m_is_lsb = false;
		break;
	default:
		return false;
		break;
	}
	m_sample_count = fmt.sample_count;
	m_block_size = fmt.block_size;
	m_sample_in_block = 0;
	m_block_data_end = 0;
	m_file->seek(pos + hton64(fmt.get_size()));
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck == "data")) {
		return false;
	}
	m_block_data.resize(m_channel_count * m_block_size);
	m_data_offset = m_file->get_position();
	m_data_size = hton64(ck.get_size()) - sizeof(ck);
	m_data_end_offset = m_data_offset + std::min(get_size(), m_data_size);
	if (m_id3_offset != 0) {
		m_file->seek(m_id3_offset);
		id3_tags_t id3_tags;
		id3_tags.value.resize((size_t)(m_file_size - m_id3_offset));
		m_file->read(id3_tags.value.data(), id3_tags.value.size());
		m_id3_tagger.append(id3_tags);
		m_file->seek(m_data_offset);
	}
	m_id3_tagger.set_single_track(true);
	return true;
}

bool udsd_reader_dsf_t::close() {
	m_id3_tagger.remove_all();
	return true;
}

bool udsd_reader_dsf_t::select_track(uint32_t track_number) {
	m_track_start_offset = m_data_offset;
	m_track_end_offset = m_data_end_offset;
	return m_file->seek(m_track_start_offset);
}

std::tuple<bool, size_t, frame_type_e, frame_span_e> udsd_reader_dsf_t::read_frame(uint8_t* frame_data, size_t frame_size) {
	auto frame_read{ size_t(0) };
	auto frame_type{ frame_type_e::INVALID };
	auto frame_span{ frame_span_e::NO_TRACK };
	auto samples_read{ size_t(0) };
	for (auto sample = 0u; sample < frame_size / m_channel_count; sample++) {
		if (m_sample_in_block >= m_block_data_end / m_channel_count) {
			if (m_block_data_end > 0) {
				m_sample_in_block = 0;
			}
			auto bytes_left = m_track_end_offset - m_file->get_position();
			if (bytes_left > 0) {
				m_block_data_end = (int)std::min(bytes_left, int64_t(m_block_size * m_channel_count));
				m_file->read(m_block_data.data(), m_block_size * m_channel_count);
			}
			else {
				m_block_data_end = 0;
				break;
			}
		}
		for (auto ch = 0; ch < m_channel_count; ch++) {
			auto b = m_block_data.data()[ch * m_block_size + m_sample_in_block];
			frame_data[sample * m_channel_count + ch] = m_is_lsb ? swap_bits[b] : b;
		}
		m_sample_in_block++;
		samples_read++;
	}
	frame_read = samples_read * m_channel_count;
	frame_type = samples_read > 0 ? frame_type_e::DSD : frame_type_e::INVALID;
	if (samples_read > 0) {
		frame_span = frame_span_e::IN_TRACK;
	}
	return std::make_tuple(samples_read > 0, frame_read, frame_type, frame_span);
}

bool udsd_reader_dsf_t::seek(double seconds) {
	auto sample_offset = std::min(int64_t(m_samplerate / 8 * seconds), m_track_end_offset / m_channel_count);
	sample_offset = (sample_offset / (m_samplerate / 8 / m_framerate)) * (m_samplerate / 8 / m_framerate);
	auto block_offset = (sample_offset / m_block_size) * m_block_size;
	m_sample_in_block = sample_offset % m_block_size;
	m_block_data_end = 0;
	return m_file->seek(m_track_start_offset + m_channel_count * block_offset);
}

void udsd_reader_dsf_t::get_info(uint32_t track_number, kodi::addon::AudioDecoderInfoTag& info) {
	udsd_reader_t::get_info(track_number, info);
	m_id3_tagger.get_info(track_number, info);
}

void udsd_reader_dsf_t::set_info(uint32_t track_number, const kodi::addon::AudioDecoderInfoTag& info) {
	m_id3_tagger.set_info(track_number, info);
}

void udsd_reader_dsf_t::get_albumart(uint32_t albumart_id, std::vector<uint8_t>& albumart_data) {
	m_id3_tagger.get_albumart(albumart_id, albumart_data);
}

void udsd_reader_dsf_t::set_albumart(uint32_t albumart_id, const std::vector<uint8_t>& albumart_data) {
	m_id3_tagger.set_albumart(albumart_id, albumart_data);
}

void udsd_reader_dsf_t::commit() {
	int64_t pos = m_file->get_position();
	m_file->truncate(m_id3_offset);
	m_file->seek(m_id3_offset);
	size_t tags_size = 0;
	for (auto[tag_data, tag_size] : m_id3_tagger) {
		if (tag_size > 0) {
			tags_size += m_file->write(tag_data, tag_size);
		}
	}
	if (tags_size == 0) {
		m_id3_offset = 0;
	}
	m_file->seek(20);
	m_file->write(&m_id3_offset, sizeof(m_id3_offset));
	m_file_size = m_file->get_size();
	m_file->seek(12);
	m_file->write(&m_file_size, sizeof(m_file_size));
	m_file->seek(pos);
}

int64_t udsd_reader_dsf_t::get_size() {
	return (m_sample_count / 8) * m_channel_count;
}

int64_t udsd_reader_dsf_t::get_offset() {
	return m_file->get_position() - m_track_start_offset;
}
