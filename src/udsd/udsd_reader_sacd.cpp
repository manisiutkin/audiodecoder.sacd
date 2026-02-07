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

#include "udsd_reader_sacd.h"

#include "../Settings.h"

#include <algorithm>
#include <iconv.h>
#include <kodi/General.h>
#include <sstream>

#define NO_ICONV ((iconv_t) - 1)

bool udsd_reader_sacd_t::g_is_sacd(const std::string& p_path)
{
  try
  {
    kodi::vfs::CFile file;
    char sacdmtoc[8];
    if (!file.OpenFile(p_path))
      return false;

    file.Seek(START_OF_MASTER_TOC * SACD_LSN_SIZE);
    if (file.Read(sacdmtoc, 8) == 8)
    {
      if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0)
      {
        return true;
      }
    }
    file.Seek(START_OF_MASTER_TOC * SACD_PSN_SIZE + 12);
    if (file.Read(sacdmtoc, 8) == 8)
    {
      if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0)
      {
        return true;
      }
    }
  }
  catch (...)
  {
  }
  return false;
}

static inline int has_two_channel(scarletbook_handle_t* handle) {
	return handle->twoch_area_idx != -1;
}

static inline int has_multi_channel(scarletbook_handle_t* handle) {
	return handle->mulch_area_idx != -1;
}

static inline int has_both_channels(scarletbook_handle_t* handle) {
	return handle->twoch_area_idx != -1 && handle->mulch_area_idx != -1;
}

static inline area_toc_t* get_two_channel(scarletbook_handle_t* handle) {
	return handle->twoch_area_idx == -1 ? 0 : handle->area[handle->twoch_area_idx].area_toc;
}

static inline area_toc_t* get_multi_channel(scarletbook_handle_t* handle) {
	return handle->mulch_area_idx == -1 ? 0 : handle->area[handle->mulch_area_idx].area_toc;
}

typedef struct
{
  const char* id;
  const char* name;
} codepage_id_t;

static codepage_id_t codepage_ids[] = {
    {"US-ASCII", character_set[0]},  {"ISO646-JP", character_set[1]},
    {"ISO8859-1", character_set[2]}, {"SHIFT_JIS", character_set[3]},
    {"KSC5636", character_set[4]},   {"GB2312", character_set[5]},
    {"BIG5", character_set[6]},      {"ISO8859-1", character_set[7]},
};

/* iconv may declare inbuf to be char** rather than const char** depending on platform and version,
    so provide a wrapper that handles both */
struct charPtrPtrAdapter
{
  const char** pointer;
  explicit charPtrPtrAdapter(const char** p) : pointer(p) {}
  operator char**() { return const_cast<char**>(pointer); }
  operator const char**() { return pointer; }
};

static inline std::string charset_convert(const std::string& strSource,
                                          size_t insize,
                                          uint8_t codepage_index)
{
  bool failOnInvalidChar = false;
  std::string utf8_str;
  const char* codepage_id = "US-ASCII";
  if (codepage_index < sizeof(codepage_ids) / sizeof(*codepage_ids))
  {
    codepage_id = codepage_ids[codepage_index].id;
  }

  iconv_t conv = iconv_open("UTF-8", codepage_id);
  if (conv == NO_ICONV)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: iconv_open() for \"%s\" -> \"%s\" failed, errno = %d (%s)",
              __func__, codepage_id, "UTF-8", errno, strerror(errno));
    return strSource;
  }

  //input buffer for iconv() is the buffer from strSource
  size_t inBufSize = (strSource.length() + 1) * sizeof(std::string::value_type);
  const char* inBuf = (const char*)strSource.c_str();

  //allocate output buffer for iconv()
  size_t outBufSize = (strSource.length() + 1) * sizeof(std::string::value_type) * 4;
  char* outBuf = (char*)malloc(outBufSize);
  if (outBuf == nullptr)
  {
    kodi::Log(ADDON_LOG_FATAL, "%s: malloc failed", __FUNCTION__);
    return "";
  }

  size_t inBytesAvail = inBufSize; //how many bytes iconv() can read
  size_t outBytesAvail = outBufSize; //how many bytes iconv() can write
  const char* inBufStart = inBuf; //where in our input buffer iconv() should start reading
  char* outBufStart = outBuf; //where in out output buffer iconv() should start writing

  size_t returnV;
  while (true)
  {
    //iconv() will update inBufStart, inBytesAvail, outBufStart and outBytesAvail
    returnV =
        iconv(conv, charPtrPtrAdapter(&inBufStart), &inBytesAvail, &outBufStart, &outBytesAvail);

    if (returnV == (size_t)-1)
    {
      if (errno == E2BIG) //output buffer is not big enough
      {
        //save where iconv() ended converting, realloc might make outBufStart invalid
        size_t bytesConverted = outBufSize - outBytesAvail;

        //make buffer twice as big
        outBufSize *= 2;
        char* newBuf = (char*)realloc(outBuf, outBufSize);
        if (!newBuf)
        {
          kodi::Log(ADDON_LOG_FATAL, "%s realloc failed with errno=%d(%s)", __FUNCTION__, errno,
                    strerror(errno));
          break;
        }
        outBuf = newBuf;

        //update the buffer pointer and counter
        outBufStart = outBuf + bytesConverted;
        outBytesAvail = outBufSize - bytesConverted;

        //continue in the loop and convert the rest
        continue;
      }
      else if (errno == EILSEQ) //An invalid multibyte sequence has been encountered in the input
      {
        if (failOnInvalidChar)
          break;

        //skip invalid byte
        inBufStart++;
        inBytesAvail--;
        //continue in the loop and convert the rest
        continue;
      }
      else if (errno == EINVAL) /* Invalid sequence at the end of input buffer */
      {
        if (!failOnInvalidChar)
          returnV = 0; /* reset error status to use converted part */

        break;
      }
      else //iconv() had some other error
      {
        kodi::Log(ADDON_LOG_ERROR, "%s: iconv() failed, errno=%d (%s)", __FUNCTION__, errno,
                  strerror(errno));
      }
    }
    break;
  }

  //complete the conversion (reset buffers), otherwise the current data will prefix the data on the next call
  if (iconv(conv, NULL, NULL, &outBufStart, &outBytesAvail) == (size_t)-1)
    kodi::Log(ADDON_LOG_ERROR, "%s failed cleanup errno=%d(%s)", __FUNCTION__, errno,
              strerror(errno));

  if (returnV == (size_t)-1)
  {
    free(outBuf);
    return utf8_str;
  }
  //we're done

  const typename std::string::size_type sizeInChars =
      (typename std::string::size_type)(outBufSize - outBytesAvail) /
      sizeof(std::string::value_type);
  typename std::string::const_pointer strPtr = (typename std::string::const_pointer)outBuf;
  /* Make sure that all buffer is assigned and string is stopped at end of buffer */
  if (strPtr[sizeInChars - 1] == 0 && strSource[strSource.length() - 1] != 0)
    utf8_str.assign(strPtr, sizeInChars - 1);
  else
    utf8_str.assign(strPtr, sizeInChars);

  free(outBuf);

  return utf8_str;
}

static inline int get_channel_count(audio_frame_info_t* frame_info) {
	if (frame_info->channel_bit_2 == 1 && frame_info->channel_bit_3 == 0) {
		return 6;
	}
	else if (frame_info->channel_bit_2 == 0 && frame_info->channel_bit_3 == 1) {
		return 5;
	}
	else {
		return 2;
	}
}

udsd_reader_sacd_t::udsd_reader_sacd_t() {
	m_file = nullptr;
	m_mode = ACCESS_MODE_NULL;
	m_audio_sector.header.dst_encoded = 0;
	m_dst_encoded = false;
	m_sector_bad_reads = 0;
	m_sb.area_count = 0;
	m_sb.twoch_area_idx = -1;
	m_sb.mulch_area_idx = -1;
	m_sector_size = 0;
}

udsd_reader_sacd_t::~udsd_reader_sacd_t() {
	close();
}

std::tuple<scarletbook_area_t*, uint32_t> udsd_reader_sacd_t::get_area_and_index_from_track(uint32_t track_number) {
	uint32_t twoch_count = 0;
	uint32_t mulch_count = 0;
	scarletbook_area_t* twoch_area = nullptr;
	scarletbook_area_t* mulch_area = nullptr;
	if (m_sb.twoch_area_idx != -1) {
		twoch_area = &m_sb.area[m_sb.twoch_area_idx];
		twoch_count += twoch_area->area_toc->track_count;
	}
	if (m_sb.mulch_area_idx != -1) {
		mulch_area = &m_sb.area[m_sb.mulch_area_idx];
		mulch_count += mulch_area->area_toc->track_count;
	}
	if (track_number == TRACK_SELECTED) {
		track_number = m_track_number;
	}
	if (track_number == TRACK_CUESHEET) {
		if (m_mode & ACCESS_MODE_TWOCH) {
			if (twoch_area) {
				return std::make_tuple(twoch_area, -1);
			}
		}
		if (m_mode & ACCESS_MODE_MULCH) {
			if (mulch_area) {
				return std::make_tuple(mulch_area, -1);
			}
		}
		return std::make_tuple(nullptr, -1);
	}
	if ((track_number > twoch_count) && (track_number <= twoch_count + mulch_count)) {
		if (m_mode & ACCESS_MODE_SINGLE_TRACK) {
			return std::make_tuple(mulch_area, -1);
		}
		else {
			return std::make_tuple(mulch_area, track_number - twoch_count - 1);
		}
	}
	else if ((track_number > 0) && (track_number <= twoch_count)) {
		if (m_mode & ACCESS_MODE_SINGLE_TRACK) {
			return std::make_tuple(twoch_area, -1);
		}
		else {
			return std::make_tuple(twoch_area, track_number - 1);
		}
	}
	return std::make_tuple(nullptr, -1);
}

uint32_t udsd_reader_sacd_t::get_track_count(uint32_t mode) {
	uint32_t track_mode = mode ? mode : m_mode;
	uint32_t track_count = 0;
	if (track_mode & ACCESS_MODE_TWOCH) {
		if (m_sb.twoch_area_idx != -1) {
			track_count += m_sb.area[m_sb.twoch_area_idx].area_toc->track_count;
		}
	}
	if (track_mode & ACCESS_MODE_MULCH) {
		if (m_sb.mulch_area_idx != -1) {
			track_count += m_sb.area[m_sb.mulch_area_idx].area_toc->track_count;
		}
	}
	if (track_mode & ACCESS_MODE_SINGLE_TRACK) {
		track_count = std::min(track_count, 1u);
	}
	return track_count;
}

uint32_t udsd_reader_sacd_t::get_track_number(uint32_t track_index) {
	uint32_t track_number = 1;
	if (!(m_mode & ACCESS_MODE_TWOCH)) {
		if (m_sb.twoch_area_idx != -1) {
			track_number += m_sb.area[m_sb.twoch_area_idx].area_toc->track_count;
		}
	}
	return track_number + track_index;
}

int udsd_reader_sacd_t::get_channels(uint32_t track_number) {
	auto[area, track_index] = get_area_and_index_from_track(track_number);
	return area ? area->area_toc->channel_count : 0;
}

int udsd_reader_sacd_t::get_loudspeaker_config(uint32_t track_number) {
	auto[area, track_index] = get_area_and_index_from_track(track_number);
	return area ? area->area_toc->loudspeaker_config : 0;
}

int udsd_reader_sacd_t::get_samplerate(uint32_t track_number) {
	return SACD_SAMPLING_FREQUENCY;
}

int udsd_reader_sacd_t::get_framerate(uint32_t track_number) {
	return 75;
}

double udsd_reader_sacd_t::get_duration(uint32_t track_number) {
	double duration = 0.0;
	auto[area, track_index] = get_area_and_index_from_track(track_number);
	if (area) {
		if (track_index == -1) {
			auto t = area->area_toc->total_playtime;
			duration = t.minutes * 60.0 + t.seconds + t.frames / 75.0;
		}
		else {
			auto t = area->area_tracklist_time->duration[track_index];
			duration = t.minutes * 60.0 + t.seconds + t.frames / 75.0;
		}
	}
	return duration;
}

bool udsd_reader_sacd_t::is_dst(uint32_t track_number) {
	auto[area, track_index] = get_area_and_index_from_track(track_number);
	if (area) {
		return area->area_toc->frame_format == FRAME_FORMAT_DST;
	}
	return false;
}

bool udsd_reader_sacd_t::is_gapless(uint32_t track_number) {
	auto gapless{ true };
	if (track_number == TRACK_CUESHEET) {
		return gapless;
	}
	auto [area, track_index] = get_area_and_index_from_track(track_number);
	if (area) {
		auto track_start_lsn = area->area_tracklist_offset->track_start_lsn[track_index];
		auto track_length_lsn = area->area_tracklist_offset->track_length_lsn[track_index];
		if (track_index > 0) {
			if (track_start_lsn + 1 != area->area_tracklist_offset->track_start_lsn[track_index - 1] + area->area_tracklist_offset->track_length_lsn[track_index - 1]) {
				gapless = false;
			}
		}
		if (track_index + 1 < area->area_toc->track_count) {
			if (track_start_lsn + track_length_lsn != area->area_tracklist_offset->track_start_lsn[track_index + 1] + 1) {
				gapless = false;
			}
		}
	}
	return gapless;
}

void udsd_reader_sacd_t::set_mode(uint32_t selector, bool is_set) {
	m_mode = (m_mode & ~selector) | (is_set ? selector : (uint32_t)0);
}

bool udsd_reader_sacd_t::open(udsd_media_t* media_file) {
	auto ok{ false };
	m_file = media_file;
	m_sb.area_count = 0;
	m_sb.twoch_area_idx = -1;
	m_sb.mulch_area_idx = -1;
	char sacdmtoc[8];
	m_sector_size = 0;
	m_sector_bad_reads = 0;
	m_file->seek((uint64_t)START_OF_MASTER_TOC * (uint64_t)SACD_LSN_SIZE);
	if (m_file->read(sacdmtoc, 8) == 8) {
		if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0) {
			m_sector_size = SACD_LSN_SIZE;
			m_buffer = m_sector_buffer;
		}
	}
	if (!m_file->seek((uint64_t)START_OF_MASTER_TOC * (uint64_t)SACD_PSN_SIZE + 12)) {
		goto open_exit;
	}
	if (m_file->read(sacdmtoc, 8) == 8) {
		if (memcmp(sacdmtoc, "SACDMTOC", 8) == 0) {
			m_sector_size = SACD_PSN_SIZE;
			m_buffer = m_sector_buffer + 12;
		}
	}
	if (!m_file->seek(0)) {
		goto open_exit;
	}
	if (m_sector_size == 0) {
		goto open_exit;
	}
	m_sb.master_data.resize(MASTER_TOC_LEN * SACD_LSN_SIZE);
	if (!read_blocks_raw(START_OF_MASTER_TOC, MASTER_TOC_LEN, m_sb.master_data.data())) {
		goto open_exit;
	}
	{
		//auto h_md5 = hasher_md5::get()->process_single(m_sb.master_data.data(), m_sb.master_data.size());
		//std::memcpy(m_md5, h_md5.m_data, 16);
	}
	if (!read_master_toc()) {
		goto open_exit;
	}
	if (m_sb.master_toc->area_1_toc_1_start) {
		m_sb.area[m_sb.area_count].area_data.resize(m_sb.master_toc->area_1_toc_size * SACD_LSN_SIZE);
		if (!read_blocks_raw(m_sb.master_toc->area_1_toc_1_start, m_sb.master_toc->area_1_toc_size, m_sb.area[m_sb.area_count].area_data.data())) {
			m_sb.master_toc->area_1_toc_1_start = 0;
		}
		else {
			if (read_area_toc(m_sb.area_count)) {
				m_sb.area_count++;
			}
		}
		ok = true;
	}
	if (m_sb.master_toc->area_2_toc_1_start) {
		m_sb.area[m_sb.area_count].area_data.resize(m_sb.master_toc->area_2_toc_size * SACD_LSN_SIZE);
		if (!read_blocks_raw(m_sb.master_toc->area_2_toc_1_start, m_sb.master_toc->area_2_toc_size, m_sb.area[m_sb.area_count].area_data.data())) {
			m_sb.master_toc->area_2_toc_1_start = 0;
		}
		else {
			if (read_area_toc(m_sb.area_count)) {
				m_sb.area_count++;
			}
		}
		ok = true;
	}
open_exit:
	if (!ok) {
		close();
	}
	return ok;
}

bool udsd_reader_sacd_t::close() {
	if (has_two_channel(&m_sb)) {
		free_area(&m_sb.area[m_sb.twoch_area_idx]);
		m_sb.area[m_sb.twoch_area_idx].area_data.clear();
		m_sb.twoch_area_idx = -1;
	}
	if (has_multi_channel(&m_sb))	{
		free_area(&m_sb.area[m_sb.mulch_area_idx]);
		m_sb.area[m_sb.mulch_area_idx].area_data.clear();
		m_sb.mulch_area_idx = -1;
	}
	m_sb.area_count = 0;
	master_text_t* mt = &m_sb.master_text;
	mt->album_title.clear();
	mt->album_title_phonetic.clear();
  mt->album_artist.clear();
  mt->album_artist_phonetic.clear();
  mt->album_publisher.clear();
  mt->album_publisher_phonetic.clear();
  mt->album_copyright.clear();
  mt->album_copyright_phonetic.clear();
  mt->disc_title.clear();
  mt->disc_title_phonetic.clear();
  mt->disc_artist.clear();
  mt->disc_artist_phonetic.clear();
  mt->disc_publisher.clear();
  mt->disc_publisher_phonetic.clear();
  mt->disc_copyright.clear();
  mt->disc_copyright_phonetic.clear();
	m_sb.master_data.clear();
	return true;
}

bool udsd_reader_sacd_t::select_track(uint32_t track_number) {
	auto[area, track_index] = get_area_and_index_from_track(track_number);
	if (!area) {
		return false;
	}
	m_track_number = track_number;
	m_channel_count = area->area_toc->channel_count;
	m_area_first_lsn = area->area_toc->track_start;
	m_area_last_lsn = area->area_toc->track_end;
	if (track_index == -1) {
		m_track_first_lsn = area->area_toc->track_start;
		m_track_length_lsn = area->area_toc->track_end - area->area_toc->track_start + 1;
	}
	else {
		if (m_mode & ACCESS_MODE_EDITED_MASTER_TRACK) {
			if (track_index > 0) {
				m_track_first_lsn = area->area_tracklist_offset->track_start_lsn[track_index];
			}
			else {
				m_track_first_lsn = area->area_toc->track_start;
			}
			if (track_index + 1 < area->area_toc->track_count) {
				m_track_length_lsn = area->area_tracklist_offset->track_start_lsn[track_index + 1] - m_track_first_lsn + 1;
			}
			else {
				m_track_length_lsn = area->area_toc->track_end - m_track_first_lsn + 1;
			}
		}
		else {
			m_track_first_lsn = area->area_tracklist_offset->track_start_lsn[track_index];
			m_track_length_lsn = area->area_tracklist_offset->track_length_lsn[track_index];
		}
	}
	m_read_first_lsn = m_track_first_lsn;
	m_read_length_lsn = m_track_length_lsn;
	m_read_next_lsn = m_track_first_lsn;
	m_span_frames = 0;
	m_span_first_lsn = m_read_first_lsn;
	m_span_length_lsn = m_read_length_lsn;
	m_track_complete = m_track_length_lsn < 2;
	memset(&m_audio_sector, 0, sizeof(m_audio_sector));
	memset(&m_frame, 0, sizeof(m_frame));
	m_packet_info_idx = 0;
	m_file->seek((uint64_t)m_read_next_lsn * (uint64_t)m_sector_size);
	//console::printf("udsd_reader_sacd_t::select_track(track = %d, first_lsn = %d, last_lsn = %d)", track_number, m_track_next_lsn, m_track_next_lsn + m_track_length_lsn);
	return true;
}

bool udsd_reader_sacd_t::set_track_span(uint32_t span_frames) {
	m_span_frames = span_frames;
	std::tie(m_span_first_lsn, m_span_length_lsn) = get_track_span();
	m_read_next_lsn = m_span_first_lsn;
	m_file->seek((uint64_t)m_read_next_lsn * (uint64_t)m_sector_size);
	return m_span_length_lsn > m_read_length_lsn;
}

std::tuple<bool, size_t, frame_type_e, frame_span_e> udsd_reader_sacd_t::read_frame(uint8_t* frame_data, size_t frame_size) {
	//console::printf("udsd_reader_sacd_t::read_frame start");
	auto frame_read{ size_t(0) };
	auto frame_type{ frame_type_e::INVALID };
	auto frame_span{ (m_read_next_lsn < m_read_first_lsn) ? frame_span_e::PRE_TRACK : (m_read_next_lsn < m_read_first_lsn + m_read_length_lsn) ? frame_span_e::IN_TRACK : frame_span_e::POST_TRACK };
	m_sector_bad_reads = 0;
	while (!m_track_complete) {
		if (m_sector_bad_reads > 0) {
			m_buffer_offset = 0;
			m_packet_info_idx = 0;
			memset(&m_audio_sector, 0, sizeof(m_audio_sector));
			memset(&m_frame, 0, sizeof(m_frame));
			frame_type = frame_type_e::INVALID;
			//console::printf("udsd_reader_sacd_t::read_frame.end_invalid");
			return std::make_tuple(true, frame_read, frame_type, frame_span);
		}
		if (m_packet_info_idx == m_audio_sector.header.packet_info_count) {
			if (m_read_next_lsn > m_area_last_lsn) {
				m_track_complete = true;
				//console::printf("udsd_reader_sacd_t::read_frame::track_complete");
				continue;
			}
			// obtain the sector data block
			//console::printf("udsd_reader_sacd_t::read_frame read_lsn(trk = %d, lsn = %d, span = %d)", m_track_number, m_file->get_position() / m_sector_size, frame_span);
			m_buffer_offset = 0;
			m_packet_info_idx = 0;
			size_t read_bytes = m_file->read(m_sector_buffer, m_sector_size);
			m_read_next_lsn++;
			if (read_bytes != m_sector_size) {
				m_sector_bad_reads++;
				continue;
			}
			m_audio_sector.header = *(audio_frame_header_t*)(m_buffer + m_buffer_offset);
			m_buffer_offset += AUDIO_SECTOR_HEADER_SIZE;
			for (auto i = 0; i < m_audio_sector.header.packet_info_count; i++) {
				m_audio_sector.packet[i].frame_start = ((m_buffer + m_buffer_offset)[0] >> 7) & 1;
				m_audio_sector.packet[i].data_type = ((m_buffer + m_buffer_offset)[0] >> 3) & 7;
				m_audio_sector.packet[i].packet_length = ((m_buffer + m_buffer_offset)[0] & 7) << 8 | (m_buffer + m_buffer_offset)[1];
				m_buffer_offset += AUDIO_PACKET_INFO_SIZE;
			}
			if (m_audio_sector.header.dst_encoded) {
				memcpy(m_audio_sector.frame, m_buffer + m_buffer_offset, AUDIO_FRAME_INFO_SIZE * m_audio_sector.header.frame_info_count);
				m_buffer_offset += AUDIO_FRAME_INFO_SIZE * m_audio_sector.header.frame_info_count;
			}
			else {
				for (uint8_t i = 0; i < m_audio_sector.header.frame_info_count; i++) {
					memcpy(&m_audio_sector.frame[i], m_buffer + m_buffer_offset, AUDIO_FRAME_INFO_SIZE - 1);
					m_buffer_offset += AUDIO_FRAME_INFO_SIZE - 1;
				}
			}
		}
		while (m_packet_info_idx < m_audio_sector.header.packet_info_count && m_sector_bad_reads == 0) {
			auto& packet = m_audio_sector.packet[m_packet_info_idx];
			switch (packet.data_type) {
			case DATA_TYPE_AUDIO:
				if (m_frame.started) {
					if (packet.frame_start) {
						//console::printf("udsd_reader_sacd_t::read_frame.frame_started");
						m_frame.started = false;
						if ((size_t)m_frame.size <= frame_size) {
							memcpy(frame_data, m_frame.data, m_frame.size);
							frame_read = m_frame.size;
							//console::printf("udsd_reader_sacd_t::read_frame.copy_frame_data(size = %d)", *frame_size);
						}
						else {
							m_sector_bad_reads++;
							continue;
						}
						frame_type = m_sector_bad_reads > 0 ? frame_type_e::INVALID : m_frame.dst_encoded ? frame_type_e::DST : frame_type_e::DSD;
						m_track_complete = m_read_next_lsn >= m_span_first_lsn + m_span_length_lsn - 1;
						//console::printf("udsd_reader_sacd_t::read_frame track_complete(m_span_first_lsn = %d, m_span_length_lsn = %d)", m_span_first_lsn, m_span_length_lsn);
						return std::make_tuple(true, frame_read, frame_type, frame_span);
					}
				}
				else {
					if (packet.frame_start) {
						m_frame.started = true;
						m_frame.size = 0;
						m_frame.dst_encoded = m_audio_sector.header.dst_encoded;
					}
				}
				if (m_frame.started) {
					if ((size_t)m_frame.size + packet.packet_length <= frame_size && m_buffer_offset + packet.packet_length <= SACD_LSN_SIZE) {
						memcpy(m_frame.data + m_frame.size, m_buffer + m_buffer_offset, packet.packet_length);
						m_frame.size += packet.packet_length;
						//console::printf("udsd_reader_sacd_t::read_frame append_frame_data(size = %d, total = %d)", packet.packet_length, m_frame.size);
					}
					else {
						m_sector_bad_reads++;
						continue;
					}
				}
				break;
			case DATA_TYPE_SUPPLEMENTARY:
			case DATA_TYPE_PADDING:
				break;
			default:
				break;
			}
			m_buffer_offset += packet.packet_length;
			m_packet_info_idx++;
		}
	}
	//console::printf("udsd_reader_sacd_t::read_frame.track_complete");
	if (m_frame.started) {
		if ((size_t)m_frame.size <= frame_size) {
			memcpy(frame_data, m_frame.data, m_frame.size);
			frame_read = m_frame.size;
			m_frame.started = false;
		}
		else {
			m_sector_bad_reads++;
			m_buffer_offset = 0;
			m_packet_info_idx = 0;
			memset(&m_audio_sector, 0, sizeof(m_audio_sector));
			memset(&m_frame, 0, sizeof(m_frame));
			//console::printf("udsd_reader_sacd_t::read_frame.bad_reads(count = %d)", m_sector_bad_reads);
		}
		frame_type = m_sector_bad_reads > 0 ? frame_type_e::INVALID : m_frame.dst_encoded ? frame_type_e::DST : frame_type_e::DSD;
		//console::printf("udsd_reader_sacd_t::read_frame._data(size = %d)", *frame_size);
		return std::make_tuple(true, frame_read, frame_type, frame_span);
	}
	//console::printf("udsd_reader_sacd_t::read_frame::read_frame_data(size = %d)", *frame_size);
	return std::make_tuple(false, frame_read, frame_type, frame_span);
}

bool udsd_reader_sacd_t::seek(double seconds) {
	uint64_t offset = (uint64_t)(get_size() * seconds / get_duration(TRACK_SELECTED));
	m_read_next_lsn = m_track_first_lsn + (uint32_t)(offset / m_sector_size);
	if (m_track_number == TRACK_CUESHEET) {
		auto [area, track_index] = get_area_and_index_from_lsn(m_read_next_lsn);
		if (!area) {
			return false;
		}
		if (track_index == -1) {
			m_read_first_lsn = area->area_toc->track_start;
			m_read_length_lsn = area->area_toc->track_end - area->area_toc->track_start + 1;
		}
		else {
			if (m_mode & ACCESS_MODE_EDITED_MASTER_TRACK) {
				if (track_index > 0) {
					m_read_first_lsn = area->area_tracklist_offset->track_start_lsn[track_index];
				}
				else {
					m_read_first_lsn = area->area_toc->track_start;
				}
				if (track_index + 1 < area->area_toc->track_count) {
					m_read_length_lsn = area->area_tracklist_offset->track_start_lsn[track_index + 1] - m_read_first_lsn + 1;
				}
				else {
					m_read_length_lsn = area->area_toc->track_end - m_read_first_lsn + 1;
				}
			}
			else {
				m_read_first_lsn = area->area_tracklist_offset->track_start_lsn[track_index];
				m_read_length_lsn = area->area_tracklist_offset->track_length_lsn[track_index];
			}
		}
		std::tie(m_span_first_lsn, m_span_length_lsn) = get_track_span();
		if (m_read_next_lsn == m_read_first_lsn) {
			m_read_next_lsn = m_span_first_lsn;
		}
	}
	m_track_complete = false;
	memset(&m_audio_sector, 0, sizeof(m_audio_sector));
	memset(&m_frame, 0, sizeof(m_frame));
	m_packet_info_idx = 0;
	return m_file->seek((uint64_t)m_read_next_lsn * (uint64_t)m_sector_size);
}

void udsd_reader_sacd_t::get_info(uint32_t track_number, kodi::addon::AudioDecoderInfoTag& info)
{
  auto [area, track_index] = get_area_and_index_from_track(track_number);
  if (!area)
  {
    return;
  }

  bool separateMultichannel =
      CSACDSettings::GetInstance().GetSeparateMultichannel() && area->area_toc->channel_count > 2;
  info.SetTrack(((track_index == -1) ? 1 : track_index + 1) +
                (separateMultichannel ? get_track_count(AREA_TWOCH) : 0));
  if (area->area_toc->track_count > 0)
  {
  }
  if (m_sb.master_toc->album_set_size > 1)
  {
    if (m_sb.master_toc->album_sequence_number > 0)
    {
      info.SetDisc(m_sb.master_toc->album_sequence_number);
    }
    info.SetDiscTotal(m_sb.master_toc->album_set_size);
  }
  if (m_sb.master_toc->disc_date_year > 0)
  {
    std::stringstream t;
    t << m_sb.master_toc->disc_date_year;

    if (m_sb.master_toc->disc_date_month > 0)
    {
      t << "-";
      if (m_sb.master_toc->disc_date_month < 10)
      {
        t << "0";
      }
      t << m_sb.master_toc->disc_date_month;
      if (m_sb.master_toc->disc_date_day > 0)
      {
        t << "-";
        if (m_sb.master_toc->disc_date_day < 10)
        {
          t << "0";
        }
        t << m_sb.master_toc->disc_date_day;
      }
    }

    info.SetReleaseDate(t.str());
  }

  if (!m_sb.master_text.album_title.empty())
    info.SetAlbum(m_sb.master_text.album_title);
  if (!m_sb.master_text.album_artist.empty())
    info.SetArtist(m_sb.master_text.album_artist);
  if (track_index == -1)
  {
    if (!m_sb.master_text.disc_title.empty())
      info.SetTitle(m_sb.master_text.disc_title);
  }
  else
  {
    if (!area->area_track_text[track_index].track_type_title.empty())
      info.SetTitle(area->area_track_text[track_index].track_type_title);
    //     if (!area->area_track_text[track_index].track_type_composer.empty())
    //       info.meta_set("composer", area->area_track_text[track_index].track_type_composer);
    //     if (!area->area_track_text[track_index].track_type_performer.empty())
    //       info.meta_set("performer", area->area_track_text[track_index].track_type_performer);
    if (!area->area_track_text[track_index].track_type_message.empty())
      info.SetComment(area->area_track_text[track_index].track_type_message);
    if (area->area_isrc_genre)
    {
      if (area->area_isrc_genre->track_genre[track_index].category == 1)
      {
        uint8_t genre = area->area_isrc_genre->track_genre[track_index].genre;
        if (genre > 0)
        {
          info.SetGenre(album_genre[genre]);
        }
      }
    }
  }
}

bool udsd_reader_sacd_t::read_blocks_raw(uint32_t lb_start, size_t block_count, uint8_t* data) {
	switch (m_sector_size) {
	case SACD_LSN_SIZE:
		m_file->seek((uint64_t)lb_start * (uint64_t)SACD_LSN_SIZE);
		if (m_file->read(data, block_count * SACD_LSN_SIZE) != block_count * SACD_LSN_SIZE) {
			m_sector_bad_reads++;
			return false;
		}
		break;
	case SACD_PSN_SIZE:
		for (uint32_t i = 0; i < block_count; i++) {
			m_file->seek((uint64_t)(lb_start + i) * (uint64_t)SACD_PSN_SIZE + 12);
			if (m_file->read(data + i * SACD_LSN_SIZE, SACD_LSN_SIZE) != SACD_LSN_SIZE) {
				m_sector_bad_reads++;
				return false;
			}
		}
		break;
	}
	return true;
}

void udsd_reader_sacd_t::get_md5(uint8_t md5[16]) {
	std::memcpy(md5, m_md5, 16);
}

std::string udsd_reader_sacd_t::get_md5() {
	//return pfc::format_hexdump(m_md5, 16, "");
  return std::string("---");
}

uint64_t udsd_reader_sacd_t::get_size() {
	return (uint64_t)m_track_length_lsn * (uint64_t)m_sector_size;
}

uint64_t udsd_reader_sacd_t::get_offset() {
	return (uint64_t)m_read_next_lsn * (uint64_t)m_sector_size;
}

bool udsd_reader_sacd_t::read_master_toc() {
	uint8_t*      p;
	master_toc_t* master_toc;

	master_toc = m_sb.master_toc = (master_toc_t*)m_sb.master_data.data();
	if (strncmp("SACDMTOC", master_toc->id, 8) != 0) {
		return false;
	}

	SWAP16(master_toc->album_set_size);
	SWAP16(master_toc->album_sequence_number);
	SWAP32(master_toc->area_1_toc_1_start);
	SWAP32(master_toc->area_1_toc_2_start);
	SWAP16(master_toc->area_1_toc_size);
	SWAP32(master_toc->area_2_toc_1_start);
	SWAP32(master_toc->area_2_toc_2_start);
	SWAP16(master_toc->area_2_toc_size);
	SWAP16(master_toc->disc_date_year);

	if (master_toc->version.major > SUPPORTED_VERSION_MAJOR || master_toc->version.minor > SUPPORTED_VERSION_MINOR) {
		return false;
	}

	// point to eof master header
	p = m_sb.master_data.data() + SACD_LSN_SIZE;

	// set pointers to text content
	for (int i = 0; i < MAX_LANGUAGE_COUNT; i++) {
		master_sacd_text_t* master_text = (master_sacd_text_t*)p;

		if (strncmp("SACDText", master_text->id, 8) != 0) {
			return false;
		}

		SWAP16(master_text->album_title_position);
		SWAP16(master_text->album_artist_position);
		SWAP16(master_text->album_publisher_position);
		SWAP16(master_text->album_copyright_position);
		SWAP16(master_text->album_title_phonetic_position);
		SWAP16(master_text->album_artist_phonetic_position);
		SWAP16(master_text->album_publisher_phonetic_position);
		SWAP16(master_text->album_copyright_phonetic_position);
		SWAP16(master_text->disc_title_position);
		SWAP16(master_text->disc_artist_position);
		SWAP16(master_text->disc_publisher_position);
		SWAP16(master_text->disc_copyright_position);
		SWAP16(master_text->disc_title_phonetic_position);
		SWAP16(master_text->disc_artist_phonetic_position);
		SWAP16(master_text->disc_publisher_phonetic_position);
		SWAP16(master_text->disc_copyright_phonetic_position);

		// we only use the first SACDText entry
		if (i == 0) {
			uint8_t current_charset = m_sb.master_toc->locales[i].character_set & 0x07;

			if (master_text->album_title_position)
				m_sb.master_text.album_title = charset_convert((char*)master_text + master_text->album_title_position, strlen((char*)master_text + master_text->album_title_position), current_charset);
			if (master_text->album_title_phonetic_position)
				m_sb.master_text.album_title_phonetic = charset_convert((char*)master_text + master_text->album_title_phonetic_position, strlen((char*)master_text + master_text->album_title_phonetic_position), current_charset);
			if (master_text->album_artist_position)
				m_sb.master_text.album_artist = charset_convert((char*)master_text + master_text->album_artist_position, strlen((char*)master_text + master_text->album_artist_position), current_charset);
			if (master_text->album_artist_phonetic_position)
				m_sb.master_text.album_artist_phonetic = charset_convert((char*)master_text + master_text->album_artist_phonetic_position, strlen((char*)master_text + master_text->album_artist_phonetic_position), current_charset);
			if (master_text->album_publisher_position)
				m_sb.master_text.album_publisher = charset_convert((char*)master_text + master_text->album_publisher_position, strlen((char*)master_text + master_text->album_publisher_position), current_charset);
			if (master_text->album_publisher_phonetic_position)
				m_sb.master_text.album_publisher_phonetic = charset_convert((char*)master_text + master_text->album_publisher_phonetic_position, strlen((char*)master_text + master_text->album_publisher_phonetic_position), current_charset);
			if (master_text->album_copyright_position)
				m_sb.master_text.album_copyright = charset_convert((char*)master_text + master_text->album_copyright_position, strlen((char*)master_text + master_text->album_copyright_position), current_charset);
			if (master_text->album_copyright_phonetic_position)
				m_sb.master_text.album_copyright_phonetic = charset_convert((char*)master_text + master_text->album_copyright_phonetic_position, strlen((char*)master_text + master_text->album_copyright_phonetic_position), current_charset);

			if (master_text->disc_title_position)
				m_sb.master_text.disc_title = charset_convert((char*)master_text + master_text->disc_title_position, strlen((char*)master_text + master_text->disc_title_position), current_charset);
			if (master_text->disc_title_phonetic_position)
				m_sb.master_text.disc_title_phonetic = charset_convert((char*)master_text + master_text->disc_title_phonetic_position, strlen((char*)master_text + master_text->disc_title_phonetic_position), current_charset);
			if (master_text->disc_artist_position)
				m_sb.master_text.disc_artist = charset_convert((char*)master_text + master_text->disc_artist_position, strlen((char*)master_text + master_text->disc_artist_position), current_charset);
			if (master_text->disc_artist_phonetic_position)
				m_sb.master_text.disc_artist_phonetic = charset_convert((char*)master_text + master_text->disc_artist_phonetic_position, strlen((char*)master_text + master_text->disc_artist_phonetic_position), current_charset);
			if (master_text->disc_publisher_position)
				m_sb.master_text.disc_publisher = charset_convert((char*)master_text + master_text->disc_publisher_position, strlen((char*)master_text + master_text->disc_publisher_position), current_charset);
			if (master_text->disc_publisher_phonetic_position)
				m_sb.master_text.disc_publisher_phonetic = charset_convert((char*)master_text + master_text->disc_publisher_phonetic_position, strlen((char*)master_text + master_text->disc_publisher_phonetic_position), current_charset);
			if (master_text->disc_copyright_position)
				m_sb.master_text.disc_copyright = charset_convert((char*)master_text + master_text->disc_copyright_position, strlen((char*)master_text + master_text->disc_copyright_position), current_charset);
			if (master_text->disc_copyright_phonetic_position)
				m_sb.master_text.disc_copyright_phonetic = charset_convert((char*)master_text + master_text->disc_copyright_phonetic_position, strlen((char*)master_text + master_text->disc_copyright_phonetic_position), current_charset);
		}
		p += SACD_LSN_SIZE;
	}

	m_sb.master_man = (master_man_t*)p;
	if (strncmp("SACD_Man", m_sb.master_man->id, 8) != 0) {
		return false;
	}

	return true;
}

bool udsd_reader_sacd_t::read_area_toc(int area_idx) {
	area_toc_t*         area_toc;
	uint8_t*            area_data;
	uint8_t*            p;
	int                 sacd_text_idx = 0;
	scarletbook_area_t* area = &m_sb.area[area_idx];
	uint8_t             current_charset;

	p = area_data = area->area_data.data();
	area_toc = area->area_toc = (area_toc_t*)area_data;

	if (strncmp("TWOCHTOC", area_toc->id, 8) != 0 && strncmp("MULCHTOC", area_toc->id, 8) != 0) {
		return false;
	}

	SWAP16(area_toc->size);
	SWAP32(area_toc->track_start);
	SWAP32(area_toc->track_end);
	SWAP16(area_toc->area_description_offset);
	SWAP16(area_toc->copyright_offset);
	SWAP16(area_toc->area_description_phonetic_offset);
	SWAP16(area_toc->copyright_phonetic_offset);
	SWAP32(area_toc->max_byte_rate);
	SWAP16(area_toc->track_text_offset);
	SWAP16(area_toc->index_list_offset);
	SWAP16(area_toc->access_list_offset);

	current_charset = area->area_toc->languages[sacd_text_idx].character_set & 0x07;

	if (area_toc->copyright_offset)
		area->copyright = charset_convert((char*)area_toc + area_toc->copyright_offset, strlen((char*)area_toc + area_toc->copyright_offset), current_charset);
	if (area_toc->copyright_phonetic_offset)
		area->copyright_phonetic = charset_convert((char*)area_toc + area_toc->copyright_phonetic_offset, strlen((char*)area_toc + area_toc->copyright_phonetic_offset), current_charset);
	if (area_toc->area_description_offset)
		area->description = charset_convert((char*)area_toc + area_toc->area_description_offset, strlen((char*)area_toc + area_toc->area_description_offset), current_charset);
	if (area_toc->area_description_phonetic_offset)
		area->description_phonetic = charset_convert((char*)area_toc + area_toc->area_description_phonetic_offset, strlen((char*)area_toc + area_toc->area_description_phonetic_offset), current_charset);

	if (area_toc->version.major > SUPPORTED_VERSION_MAJOR || area_toc->version.minor > SUPPORTED_VERSION_MINOR) {
		return false;
	}
	
	// is this the 2 channel?
	if (area_toc->channel_count == 2 && area_toc->loudspeaker_config == 0)
		m_sb.twoch_area_idx = area_idx;
	else
		m_sb.mulch_area_idx = area_idx;

	// Area TOC size is SACD_LSN_SIZE
	p += SACD_LSN_SIZE;

	while (p < (area_data + area_toc->size * SACD_LSN_SIZE)) {
		if (strncmp((char*)p, "SACDTTxt", 8) == 0) {
			// we discard all other SACDTTxt entries
			if (sacd_text_idx == 0) {
				for (uint8_t i = 0; i < area_toc->track_count; i++) {
					area_text_t* area_text;
					uint8_t      track_type, track_amount;
					char*        track_ptr;
					area_text = area->area_text = (area_text_t*)p;
					SWAP16(area_text->track_text_position[i]);
					if (area_text->track_text_position[i] > 0) {
						track_ptr = (char*)(p + area_text->track_text_position[i]);
						track_amount = *track_ptr;
						track_ptr += 4;
						for (uint8_t j = 0; j < track_amount; j++) {
							track_type = *track_ptr;
							track_ptr++;
							track_ptr++;                         // skip unknown 0x20
							if (*track_ptr != 0) {
								switch (track_type) {
								case TRACK_TYPE_TITLE:
									area->area_track_text[i].track_type_title = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_PERFORMER:
									area->area_track_text[i].track_type_performer = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_SONGWRITER:
									area->area_track_text[i].track_type_songwriter = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_COMPOSER:
									area->area_track_text[i].track_type_composer = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_ARRANGER:
									area->area_track_text[i].track_type_arranger = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_MESSAGE:
									area->area_track_text[i].track_type_message = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_EXTRA_MESSAGE:
									area->area_track_text[i].track_type_extra_message = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_TITLE_PHONETIC:
									area->area_track_text[i].track_type_title_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_PERFORMER_PHONETIC:
									area->area_track_text[i].track_type_performer_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_SONGWRITER_PHONETIC:
									area->area_track_text[i].track_type_songwriter_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_COMPOSER_PHONETIC:
									area->area_track_text[i].track_type_composer_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_ARRANGER_PHONETIC:
									area->area_track_text[i].track_type_arranger_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_MESSAGE_PHONETIC:
									area->area_track_text[i].track_type_message_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								case TRACK_TYPE_EXTRA_MESSAGE_PHONETIC:
									area->area_track_text[i].track_type_extra_message_phonetic = charset_convert(track_ptr, strlen(track_ptr), current_charset);
									break;
								}
							}
							if (j < track_amount - 1) {
								while (*track_ptr != 0)
									track_ptr++;
								while (*track_ptr == 0)
									track_ptr++;
							}
						}
					}
				}
			}
			sacd_text_idx++;
			p += SACD_LSN_SIZE;
		}
		else if (strncmp((char*)p, "SACD_IGL", 8) == 0) {
			area->area_isrc_genre = (area_isrc_genre_t*)p;
			p += SACD_LSN_SIZE * 2;
		}
		else if (strncmp((char*)p, "SACD_ACC", 8) == 0) {
			// skip
			p += SACD_LSN_SIZE * 32;
		}
		else if (strncmp((char*)p, "SACDTRL1", 8) == 0) {
			area_tracklist_offset_t* tracklist;
			tracklist = area->area_tracklist_offset = (area_tracklist_offset_t*)p;
			for (uint8_t i = 0; i < area_toc->track_count; i++) {
				SWAP32(tracklist->track_start_lsn[i]);
				SWAP32(tracklist->track_length_lsn[i]);
			}
			p += SACD_LSN_SIZE;
		}
		else if (strncmp((char*)p, "SACDTRL2", 8) == 0) {
			area_tracklist_time_t* tracklist;
			tracklist = area->area_tracklist_time = (area_tracklist_time_t*)p;
			p += SACD_LSN_SIZE;
		}
		else {
			break;
		}
	}
	return true;
}

void udsd_reader_sacd_t::free_area(scarletbook_area_t* area) {
	for (uint8_t i = 0; i < area->area_toc->track_count; i++) {
		area->area_track_text[i].track_type_title.clear();
		area->area_track_text[i].track_type_performer.clear();
		area->area_track_text[i].track_type_songwriter.clear();
	  area->area_track_text[i].track_type_composer.clear();
    area->area_track_text[i].track_type_arranger.clear();
    area->area_track_text[i].track_type_message.clear();
    area->area_track_text[i].track_type_extra_message.clear();
    area->area_track_text[i].track_type_title_phonetic.clear();
    area->area_track_text[i].track_type_performer_phonetic.clear();
    area->area_track_text[i].track_type_songwriter_phonetic.clear();
    area->area_track_text[i].track_type_composer_phonetic.clear();
    area->area_track_text[i].track_type_arranger_phonetic.clear();
    area->area_track_text[i].track_type_message_phonetic.clear();
    area->area_track_text[i].track_type_extra_message_phonetic.clear();
	}
	area->description.clear();
	area->copyright.clear();
	area->description_phonetic.clear();
	area->copyright_phonetic.clear();
}

uint32_t udsd_reader_sacd_t::get_frame_start_count(uint32_t lsn) {
	uint32_t frame_start_count{ 0 };
	// go to lsn
	m_file->seek((uint64_t)lsn * (uint64_t)m_sector_size);
	// obtain sector data block
	uint8_t        sector_buffer[SACD_PSN_SIZE];
	audio_sector_t audio_sector;
	m_file->read(sector_buffer, m_sector_size);
	auto buffer = &sector_buffer[m_sector_size - SACD_LSN_SIZE];
	audio_sector.header = *(audio_frame_header_t*)(buffer);
	auto buffer_offset = AUDIO_SECTOR_HEADER_SIZE;
	for (auto i = 0; i < audio_sector.header.packet_info_count; i++) {
		audio_sector.packet[i].frame_start = ((buffer + buffer_offset)[0] >> 7) & 1;
		audio_sector.packet[i].data_type = ((buffer + buffer_offset)[0] >> 3) & 7;
		audio_sector.packet[i].packet_length = ((buffer + buffer_offset)[0] & 7) << 8 | (buffer + buffer_offset)[1];
		buffer_offset += AUDIO_PACKET_INFO_SIZE;
		if (audio_sector.packet[i].frame_start == 1 && audio_sector.packet[i].data_type == DATA_TYPE_AUDIO) {
			frame_start_count++;
		}
	}
	return frame_start_count;
}

std::tuple<uint32_t, uint32_t> udsd_reader_sacd_t::get_track_span() {
	auto span_first_lsn{ m_read_first_lsn };
	auto l_frames{ uint32_t(0) };
	while (l_frames < m_span_frames) {
		if (span_first_lsn > m_area_first_lsn) {
			l_frames += get_frame_start_count(--span_first_lsn);
		}
		else {
			break;
		}
	}
	auto span_length_lsn{ m_read_length_lsn + (m_read_first_lsn - span_first_lsn) };
	auto r_frames{ uint32_t(0) };
	while (r_frames < m_span_frames) {
		if (span_first_lsn + span_length_lsn <= m_area_last_lsn) {
			r_frames += get_frame_start_count(span_first_lsn + span_length_lsn++);
		}
		else {
			break;
		}
	}
	return std::make_tuple(span_first_lsn, span_length_lsn);
}

std::tuple<scarletbook_area_t*, uint32_t> udsd_reader_sacd_t::get_area_and_index_from_lsn(uint32_t lsn) {
	auto track_count{ get_track_count(m_mode & ~ACCESS_MODE_SINGLE_TRACK) };
	for (auto i = 0u; i < track_count; i++) {
		auto [area, track_index] = get_area_and_index_from_track(i + 1);
		if (!area) {
			return std::make_tuple(area, track_index);
		}
		uint32_t track_first_lsn;
		uint32_t track_length_lsn;
		if (m_mode & ACCESS_MODE_EDITED_MASTER_TRACK) {
			if (track_index > 0) {
				track_first_lsn = area->area_tracklist_offset->track_start_lsn[track_index];
			}
			else {
				track_first_lsn = area->area_toc->track_start;
			}
			if (track_index + 1 < area->area_toc->track_count) {
				track_length_lsn = area->area_tracklist_offset->track_start_lsn[track_index + 1] - track_first_lsn + 1;
			}
			else {
				track_length_lsn = area->area_toc->track_end - track_first_lsn + 1;
			}
		}
		else {
			track_first_lsn = area->area_tracklist_offset->track_start_lsn[track_index];
			track_length_lsn = area->area_tracklist_offset->track_length_lsn[track_index];
		}
		if (lsn >= track_first_lsn && lsn < track_first_lsn + track_length_lsn - 1) {
			return std::make_tuple(area, track_index);
		}
	}
	return std::make_tuple(nullptr, 0);
}
