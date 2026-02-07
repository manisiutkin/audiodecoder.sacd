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

#include "udsd_reader_dsdiff.h"

#include <algorithm>

udsd_reader_dsdiff_t::udsd_reader_dsdiff_t() {
	m_mode = ACCESS_MODE_NULL;
	m_track_number = 0;
	m_id3_offset = 0;
	m_frame_nr = 0;
}

udsd_reader_dsdiff_t::~udsd_reader_dsdiff_t() {
	close();
}

uint32_t udsd_reader_dsdiff_t::get_track_count(uint32_t mode) {
	uint32_t track_mode = mode ? mode : m_mode;
	size_t track_count = 0;
	if (track_mode & ACCESS_MODE_TWOCH) {
		if (m_channel_count <= 2) {
			track_count += m_tracklist.size();
		}
	}
	if (track_mode & ACCESS_MODE_MULCH) {
		if (m_channel_count > 2) {
			track_count += m_tracklist.size();
		}
	}
	if (track_mode & ACCESS_MODE_SINGLE_TRACK) {
		track_count = std::min(track_count, size_t(1));
	}
	return uint32_t(track_count);
}

uint32_t udsd_reader_dsdiff_t::get_track_number(uint32_t track_index) {
	return (get_track_count(0) > 1) ? track_index + 1 : track_index;
}

int udsd_reader_dsdiff_t::get_channels(uint32_t track_number) {
	return m_channel_count;
}

int udsd_reader_dsdiff_t::get_loudspeaker_config(uint32_t track_number) {
	return m_loudspeaker_config;
}

int udsd_reader_dsdiff_t::get_samplerate(uint32_t track_number) {
	return m_samplerate;
}

int udsd_reader_dsdiff_t::get_framerate(uint32_t track_number) {
	return m_framerate;
}

double udsd_reader_dsdiff_t::get_duration(uint32_t track_number) {
	if (track_number == TRACK_SELECTED) {
		track_number = m_track_number;
	}
	double duration = m_dst_encoded ? (double)m_frame_count / (double)m_framerate : (double)(m_data_size / m_channel_count) * 8 / m_samplerate;
	uint32_t track_index = track_number - 1;
	if (track_index != -1) {
		if (track_index < m_tracklist.size()) {
			duration = m_tracklist[track_index].stop_time - m_tracklist[track_index].start_time;
		}
	}
	return duration;
}

bool udsd_reader_dsdiff_t::is_dst(uint32_t track_number) {
	return m_dst_encoded;
}

bool udsd_reader_dsdiff_t::is_gapless(uint32_t track_number) {
	auto gapless{ true };
	if (track_number == TRACK_CUESHEET) {
		return gapless;
	}
	auto [start_time, stop_time] = get_track_times(m_track_number);
	uint32_t track_index = (track_number == TRACK_SELECTED ? m_track_number : track_number) - 1;
	if (track_index < m_tracklist.size()) {
		if (track_index > 0) {
			if (start_time != m_tracklist[track_index - 1].stop_time) {
				gapless = false;
			}
		}
		if (track_index < m_tracklist.size() - 1) {
			if (stop_time != m_tracklist[track_index + 1].start_time) {
				gapless = false;
			}
		}
	}
	return gapless;
}

void udsd_reader_dsdiff_t::set_mode(uint32_t selector, bool is_set) {
	m_mode = (m_mode & ~selector) | (is_set ? selector : (uint32_t)0);
}

bool udsd_reader_dsdiff_t::open(udsd_media_t* p_file) {
	m_file = p_file;
	m_dsti_size = 0;
	m_id3_offset = 0;
	uint32_t start_mark_count = 0;
	Chunk ck;
	ID id;
	if (!m_file->seek(0)) {
		return false;
	}
	if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck == "FRM8")) {
		return false;
	}
	if (!(m_file->read(&id, sizeof(id)) == sizeof(id) && id == "DSD ")) {
		return false;
	}
	m_frm8_size = ck.get_size();
	m_id3_offset = sizeof(ck) + ck.get_size();
	uint64_t pos;
	while ((pos = (uint64_t)m_file->get_position()) < m_frm8_size + sizeof(ck)) {
		if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
			return false;
		}
		if (ck == "FVER" && ck.get_size() == 4) {
			uint32_t version;
			if (!(m_file->read(&version, sizeof(version)) == sizeof(version))) {
				return false;
			}
			m_version = hton32(version);
		}
		else if (ck == "PROP") {
			if (!(m_file->read(&id, sizeof(id)) == sizeof(id) && id == "SND ")) {
				return false;
			}
			int64_t id_prop_end = m_file->get_position() - sizeof(id) + ck.get_size();
			while (m_file->get_position() < id_prop_end) {
				if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
					return false;
				}
				if (ck == "FS  " && ck.get_size() == 4) {
					uint32_t samplerate;
					if (!(m_file->read(&samplerate, sizeof(samplerate)) == sizeof(samplerate))) {
						return false;
					}
					m_samplerate = hton32(samplerate);
				}
				else if (ck == "CHNL") {
					uint16_t channel_count;
					if (!(m_file->read(&channel_count, sizeof(channel_count)) == sizeof(channel_count))) {
						return false;
					}
					m_channel_count = hton16(channel_count);
					switch (m_channel_count) {
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
						m_loudspeaker_config = 3; 
						break;
					case 6:
						m_loudspeaker_config = 4; 
						break;
					default:
						m_loudspeaker_config = 65535; 
						break;
					}
					m_file->skip(ck.get_size() - sizeof(channel_count));
				}
				else if (ck == "CMPR") {
					if (!(m_file->read(&id, sizeof(id)) == sizeof(id))) {
						return false;
					}
					if (id == "DSD ") {
						m_dst_encoded = false;
					}
					if (id == "DST ") {
						m_dst_encoded = true;
					}
					m_file->skip(ck.get_size() - sizeof(id));
				}
				else if (ck == "LSCO") {
					uint16_t loudspeaker_config;
					if (!(m_file->read(&loudspeaker_config, sizeof(loudspeaker_config)) == sizeof(loudspeaker_config))) {
						return false;
					}
					m_loudspeaker_config = hton16(loudspeaker_config);
					m_file->skip(ck.get_size() - sizeof(loudspeaker_config));
				}
				else {
					m_file->skip(ck.get_size());
				}
				m_file->skip(m_file->get_position() & 1);
			}
		}
		else if (ck == "DSD ") {
			m_data_offset = m_file->get_position();
			m_data_size = ck.get_size();
			m_framerate = 75;
			m_frame_size = m_samplerate / 8 * m_channel_count / m_framerate;
			m_frame_count = (uint32_t)((m_data_size + (m_frame_size - 1)) / m_frame_size);
			m_file->skip(ck.get_size());
			track_time_t t;
			t.start_time = 0.0;
			t.stop_time = (double)(m_data_size / m_channel_count) * 8 / m_samplerate;
			m_tracklist.push_back(t);
		}
		else if (ck == "DST ") {
			m_data_offset = m_file->get_position();
			m_data_size = ck.get_size();
			if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck) && ck == "FRTE" && ck.get_size() == 6)) {
				return false;
			}
			m_data_offset += sizeof(ck) + ck.get_size();
			m_data_size -= sizeof(ck) + ck.get_size();
			uint32_t frame_count;
			if (!(m_file->read(&frame_count, sizeof(frame_count)) == sizeof(frame_count))) {
				return false;
			}
			m_frame_count = hton32(frame_count);
			uint16_t framerate;
			if (!(m_file->read(&framerate, sizeof(framerate)) == sizeof(framerate))) {
				return false;
			}
			m_framerate = hton16(framerate);
			m_frame_size = m_samplerate / 8 * m_channel_count / m_framerate;
			m_file->seek(m_data_offset + m_data_size);
			track_time_t s;
			s.start_time = 0.0;
			s.stop_time = (double)m_frame_count / m_framerate;
			m_tracklist.push_back(s);
		}
		else if (ck == "DSTI") {
			m_dsti_offset = m_file->get_position();
			m_dsti_size = ck.get_size();
			m_framelist.resize((size_t)m_dsti_size / sizeof(DSTFrameIndex));
			if (!(m_file->read(m_framelist.data(), (size_t)m_dsti_size) == (size_t)m_dsti_size)) {
				return false;
			}
			for (auto& index : m_framelist) {
				index.offset = hton64(index.offset);
				index.length = hton32(index.length);
			}
		}
		else if (ck == "DIIN") {
			int64_t id_diin_end = m_file->get_position() + ck.get_size();
			while (m_file->get_position() < id_diin_end) {
				if (!(m_file->read(&ck, sizeof(ck)) == sizeof(ck))) {
					return false;
				}
				if (ck == "MARK" && ck.get_size() >= sizeof(Marker)) {
					Marker m;
					if (m_file->read(&m, sizeof(Marker)) == sizeof(Marker)) {
						m.hours       = hton16(m.hours);
						m.samples     = hton32(m.samples);
						m.offset      = hton32(m.offset);
						m.markType    = hton16(m.markType);
						m.markChannel = hton16(m.markChannel);
						m.TrackFlags  = hton16(m.TrackFlags);
						m.count       = hton32(m.count);
						switch (m.markType) {
						case TrackStart:
							if (start_mark_count > 0) {
								track_time_t t;
								m_tracklist.push_back(t);
							}
							start_mark_count++;
							if (m_tracklist.size() > 0) {
								m_tracklist[m_tracklist.size() - 1].start_time = get_marker_time(m, m_samplerate);
								m_tracklist[m_tracklist.size() - 1].stop_time  = m_dst_encoded ? (double)m_frame_count / (double)m_framerate : (double)(m_data_size / m_channel_count) * 8 / m_samplerate;
								if (m_tracklist.size() - 1 > 0) {
									if (m_tracklist[m_tracklist.size() - 2].stop_time > m_tracklist[m_tracklist.size() - 1].start_time) {
										m_tracklist[m_tracklist.size() - 2].stop_time =  m_tracklist[m_tracklist.size() - 1].start_time;
									}
								}
							}
							break;
						case TrackStop:
							if (m_tracklist.size() > 0) {
								m_tracklist[m_tracklist.size() - 1].stop_time = get_marker_time(m, m_samplerate);
							}
							break;
						}
					}
					m_file->skip(ck.get_size() - sizeof(Marker));
				}
				else {
					m_file->skip(ck.get_size());
				}
				m_file->skip(m_file->get_position() & 1);
			}
		}
		else if (ck == "ID3 ") {
			m_id3_offset = std::min(m_id3_offset, (uint64_t)m_file->get_position() - sizeof(ck));
			id3_tags_t id3_tags;
			id3_tags.value.resize((size_t)ck.get_size());
			m_file->read(id3_tags.value.data(), id3_tags.value.size());
			m_id3_tagger.append(id3_tags);
		}
		else {
			m_file->skip(ck.get_size());
		}
		m_file->skip(m_file->get_position() & 1);
	}
	m_id3_tagger.set_single_track(m_tracklist.size() == 1);
	m_file->seek(m_data_offset);
	return m_tracklist.size() > 0;
}

bool udsd_reader_dsdiff_t::close() {
	m_track_number = 0;
	m_tracklist.resize(0);
	m_id3_tagger.remove_all();
	m_dsti_size = 0;
	m_id3_offset = 0;
	return true;
}

bool udsd_reader_dsdiff_t::select_track(uint32_t track_number) {
	m_track_number = track_number;
	auto [start_time, stop_time] = get_track_times(track_number);
	m_track_start = get_file_position_from_time(start_time);
	m_track_end = get_file_position_from_time(stop_time);
	m_read_start = m_track_start;
	m_read_end = m_track_end;
	m_span_start = m_read_start;
	m_span_end = m_read_end;
	m_file->seek(m_read_start);
	return true;
}

bool udsd_reader_dsdiff_t::set_track_span(uint32_t span_frames) {
	m_span_frames = span_frames;
	std::tie(m_span_start, m_span_end) = get_track_span(m_track_number);
	m_file->seek(m_span_start);
	return (m_span_start < m_track_start) || (m_span_end > m_track_end);
}

std::tuple<bool, size_t, frame_type_e, frame_span_e> udsd_reader_dsdiff_t::read_frame(uint8_t* frame_data, size_t frame_size) {
	auto frame_read{ size_t(0) };
	auto frame_type{ frame_type_e::INVALID };
	auto frame_span{ frame_span_e::NO_TRACK };
	if (m_dst_encoded) {
		Chunk ck;
		for (;;) {
			auto frame_pos{ uint64_t(m_file->get_position()) };
			if ((frame_pos >= m_span_start) && (frame_pos < m_span_end)) {
				if (m_file->read(&ck, sizeof(ck)) == sizeof(ck)) {
					if (ck == "DSTF" && ck.get_size() <= (uint64_t)frame_size) {
						if (m_file->read(frame_data, (size_t)ck.get_size()) == ck.get_size()) {
							m_file->skip(ck.get_size() & 1);
							frame_read = (size_t)ck.get_size();
							frame_type = frame_type_e::DST;
							frame_span = (frame_pos < m_track_start) ? frame_span_e::PRE_TRACK : (frame_pos < m_track_end) ? frame_span_e::IN_TRACK : frame_span_e::POST_TRACK;
							//console::printf("sacd_dsdiff_t::read_frame DST frame_nr = %d", get_frame_from_file_position(frame_pos));
							return std::make_tuple(true, frame_read, frame_type, frame_span);
						}
						break;
					}
					else if (ck == "DSTC" && ck.get_size() == 4) {
						uint32_t crc;
						if (ck.get_size() == sizeof(crc)) {
							if (m_file->read(&crc, sizeof(crc)) != sizeof(crc)) {
								break;
							}
						}
						else {
							m_file->skip(ck.get_size());
							m_file->skip(ck.get_size() & 1);
						}
					}
					else {
						m_file->seek(1 - (int)sizeof(ck), SEEK_CUR);
					}
				}
				else {
					break;
				}
			}
			else {
				break;
			}
		}
	}
	else {
		auto frame_pos{ uint64_t(m_file->get_position()) };
		if ((frame_pos - m_data_offset) % m_frame_size) {
			//console::printf("sacd_dsdiff_t::read_frame ERROR inexact frame_pos = [%d]", (frame_pos - m_data_offset) % m_frame_size);
		}
		if ((frame_pos >= m_span_start) && (frame_pos < m_span_end)) {
			frame_read = size_t(std::min(uint64_t(frame_size), m_span_end - frame_pos));
			if (frame_read > 0) {
				frame_read = m_file->read(frame_data, frame_read);
				frame_read -= frame_read % m_channel_count;
				if (frame_read > 0) {
					frame_type = frame_type_e::DSD;
					frame_span = (frame_pos < m_track_start) ? frame_span_e::PRE_TRACK : (frame_pos < m_track_end) ? frame_span_e::IN_TRACK : frame_span_e::POST_TRACK;
					//console::printf("sacd_dsdiff_t::read_frame DSD frame_nr = %d", get_frame_from_file_position(frame_pos));
					return std::make_tuple(true, frame_read, frame_type, frame_span);
				}
			}
		}
	}
	return std::make_tuple(false, frame_read, frame_type, frame_span);
}

bool udsd_reader_dsdiff_t::seek(double seconds) {
	auto [start_time, stop_time] = get_track_times(m_track_number);
	uint64_t file_position = get_file_position_from_time(start_time + seconds);
	if (m_track_number == TRACK_CUESHEET) {
		auto track_number = get_track_from_time(seconds);
		auto [start_time, stop_time] = get_track_times(track_number);
		m_read_start = get_file_position_from_time(start_time);
		m_read_end = get_file_position_from_time(stop_time);
		std::tie(m_span_start, m_span_end) = get_track_span(track_number);
		if (file_position == m_read_start) {
			file_position = m_span_start;
		}
	}
	//console::printf("sacd_dsdiff_t::seek frame_nr = %d", get_frame_from_file_position(file_position));
	return m_file->seek(file_position);
}

void udsd_reader_dsdiff_t::get_info(uint32_t track_number, kodi::addon::AudioDecoderInfoTag& info)
{
	udsd_reader_t::get_info(track_number, info);
	m_id3_tagger.get_info(track_number, info);
}

void udsd_reader_dsdiff_t::set_info(uint32_t track_number, const kodi::addon::AudioDecoderInfoTag& info)
{
	m_id3_tagger.set_info(track_number, info);
}

void udsd_reader_dsdiff_t::get_albumart(uint32_t albumart_id, std::vector<uint8_t>& albumart_data) {
	m_id3_tagger.get_albumart(albumart_id, albumart_data);
}

void udsd_reader_dsdiff_t::set_albumart(uint32_t albumart_id, const std::vector<uint8_t>& albumart_data) {
	m_id3_tagger.set_albumart(albumart_id, albumart_data);
}

void udsd_reader_dsdiff_t::commit() {
	m_file->truncate(m_id3_offset);
	m_file->seek(m_id3_offset);
	for (auto[tag_data, tag_size] : m_id3_tagger) {
		if (tag_size > 0) {
			write_id3_tag(tag_data, tag_size);
		}
	}
	Chunk ck;
	ck.set_id("FRM8");
	ck.set_size(m_file->get_position() - sizeof(Chunk));
	m_file->seek(0);
	m_file->write(&ck, sizeof(ck));
}

std::tuple<double, double> udsd_reader_dsdiff_t::get_track_times(uint32_t track_number) {
	uint32_t track_index = track_number - 1;
	double track_duration = m_dst_encoded ? (double)m_frame_count / (double)m_framerate : (double)(m_data_size / m_channel_count) * 8 / m_samplerate;
	double start_time = 0.0;
	double stop_time = track_duration;
	if (track_index < m_tracklist.size()) {
		if (m_mode & ACCESS_MODE_EDITED_MASTER_TRACK) {
			if (track_index > 0) {
				start_time = m_tracklist[track_index].start_time;
			}
			if (track_index + 1 < m_tracklist.size()) {
				stop_time = m_tracklist[track_index + 1].start_time;
			}
		}
		else {
			start_time = m_tracklist[track_index].start_time;
			stop_time = m_tracklist[track_index].stop_time;
		}
	}
	return std::make_tuple(start_time, stop_time);
}

std::tuple<uint64_t, uint64_t> udsd_reader_dsdiff_t::get_track_span(uint32_t track_number) {
	auto span_time = (double)m_span_frames / (double)m_framerate;
	auto [start_time, stop_time] = get_track_times(track_number);
	auto span_start = get_file_position_from_time(start_time - span_time);
	auto span_end = get_file_position_from_time(stop_time + span_time);
	return std::make_tuple(span_start, span_end);
}

uint64_t udsd_reader_dsdiff_t::get_dsti_from_frame(uint32_t frame_nr) {
	frame_nr = std::min(frame_nr, uint32_t(m_framelist.size()) - 1);
	auto index = m_framelist[frame_nr];
	return index.offset - sizeof(Chunk);
}

uint32_t udsd_reader_dsdiff_t::get_frame_from_file_position(uint64_t file_pos) {
	auto offset = file_pos + sizeof(Chunk);
	for (uint32_t i = 0; i < m_framelist.size(); i++) {
		uint32_t frame = (i + m_frame_nr) % m_framelist.size();
		if (m_framelist[frame].offset == offset) {
			m_frame_nr++;
			return frame;
		}
	}
	return uint32_t((file_pos - m_data_offset) / m_frame_size);
}

uint64_t udsd_reader_dsdiff_t::get_file_position_from_time(double seconds) {
	auto dstf_offset{ uint64_t(0) };
	if (seconds > 0.0) {
		if (m_dst_encoded) {
			dstf_offset = (uint64_t)(seconds * m_framerate / m_frame_count * m_data_size);
			if (!m_framelist.empty()) {
    			auto frame_nr = uint32_t(std::round(seconds * m_framerate));
				if (frame_nr < m_frame_count) {
					dstf_offset = get_dsti_from_frame(frame_nr) - m_data_offset;
				}
				else {
					dstf_offset = m_data_size;
				}
			}
		}
		else {
			dstf_offset = (uint64_t(seconds * m_samplerate / 8 * m_channel_count) / m_frame_size) * m_frame_size;
		}
	}
	return m_data_offset + dstf_offset;
}

uint32_t udsd_reader_dsdiff_t::get_track_from_time(double seconds) {
	for (uint32_t track_index = 0; track_index < m_tracklist.size(); track_index++) {
		auto start_time = m_tracklist[track_index].start_time;
		auto stop_time = m_tracklist[track_index].stop_time;
		if (m_mode & ACCESS_MODE_EDITED_MASTER_TRACK) {
			if (track_index + 1 < m_tracklist.size()) {
				stop_time = m_tracklist[track_index + 1].start_time;
			}
		}
		if ((seconds >= start_time) && (seconds < stop_time)) {
			return track_index + 1;
		}
	}
	return 1;
}

void udsd_reader_dsdiff_t::write_id3_tag(const void* data, size_t size) {
	Chunk ck;
	ck.set_id("ID3 ");
	ck.set_size(size);
	m_file->write(&ck, sizeof(ck));
	m_file->write(data, size);
	if (m_file->get_position() & 1) {
		const uint8_t c = 0;
		m_file->write(&c, 1);
	}
}
