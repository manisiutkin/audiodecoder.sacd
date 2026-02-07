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

#include "dsdpcm_wrapper.h"
#include <algorithm>

dsdpcm_decoder_t dsdpcm_wrapper_t::s_playback;
size_t dsdpcm_wrapper_t::s_channels{ 0 };
size_t dsdpcm_wrapper_t::s_framerate{ 0 };
size_t dsdpcm_wrapper_t::s_dsd_samplerate{ 0 };
size_t dsdpcm_wrapper_t::s_pcm_samplerate{ 0 };
bool dsdpcm_wrapper_t::s_req_init{ false };
bool dsdpcm_wrapper_t::s_use_ramp{ false };
bool dsdpcm_wrapper_t::s_use_correction{ false };
double dsdpcm_wrapper_t::s_ramp_duration{ 0.0 };
double dsdpcm_wrapper_t::s_ramp_delay{ 0.0 };
double dsdpcm_wrapper_t::s_ramp_timedown{ 0.0 };

void dsdpcm_wrapper_t::req_init() {
	s_req_init = true;
}

bool dsdpcm_wrapper_t::use_ramp() {
	return s_use_ramp;
}

void dsdpcm_wrapper_t::set_ramp(bool p_use) {
	s_use_ramp = p_use;
}

void dsdpcm_wrapper_t::set_ramp(double p_duration) {
	s_ramp_duration = p_duration;
}

void dsdpcm_wrapper_t::set_payback(bool p_use) {
	m_use_playback = p_use;
}

double dsdpcm_wrapper_t::get_delay() {
	return m_use_playback ? s_playback.get_delay() : m_convert.get_delay();
}

size_t dsdpcm_wrapper_t::get_decoder_samplerate() {
	return m_decoder_samplerate;
}

int dsdpcm_wrapper_t::init(size_t p_channels, size_t p_framerate, size_t p_dsd_samplerate, size_t p_pcm_samplerate, conv_type_e p_conv_type, bool p_conv_fp64, double* p_fir_data, size_t p_fir_size, size_t p_fir_decimation) {
	auto dsd_is_44k{ p_dsd_samplerate % 44100 == 0 };
	auto pcm_is_44k{ p_pcm_samplerate % 44100 == 0 };
	auto dsd_is_48k{ p_dsd_samplerate % 48000 == 0 };
	auto pcm_is_48k{ p_pcm_samplerate % 48000 == 0 };
	auto dsd_to_pcm_ratio{ p_dsd_samplerate / p_pcm_samplerate };
	if (dsd_is_48k && pcm_is_44k) {
		dsd_to_pcm_ratio = p_dsd_samplerate / ((p_pcm_samplerate / 44100) * 48000);
	}
	if (dsd_is_44k && pcm_is_48k) {
		dsd_to_pcm_ratio = p_dsd_samplerate / ((p_pcm_samplerate / 48000) * 44100);
	}
	int rc{ -1 };
	if (p_dsd_samplerate / dsd_to_pcm_ratio != p_pcm_samplerate) {
		return rc;
	}
	m_decoder_samplerate = p_dsd_samplerate / dsd_to_pcm_ratio;
	if (m_use_playback) {
		if (s_req_init || !(s_channels == p_channels && s_framerate == p_framerate && s_dsd_samplerate == p_dsd_samplerate && s_pcm_samplerate == p_pcm_samplerate)) {
			s_req_init = false;
			rc = s_playback.init(p_channels, p_framerate, p_dsd_samplerate, m_decoder_samplerate, p_conv_type, p_conv_fp64, p_fir_data, p_fir_size, p_fir_decimation);
			s_channels       = p_channels;
			s_framerate      = p_framerate;
			s_dsd_samplerate = p_dsd_samplerate;
			s_pcm_samplerate = p_pcm_samplerate;
			s_ramp_delay = s_playback.get_delay() / m_decoder_samplerate;
			set_ramp(true);
			s_use_correction = true;
		}
		if (use_ramp()) {
			s_ramp_timedown = s_ramp_duration;
		}
		set_ramp(true);
	}
	else {
		rc = m_convert.init(p_channels, p_framerate, p_dsd_samplerate, m_decoder_samplerate, p_conv_type, p_conv_fp64, p_fir_data, p_fir_size, p_fir_decimation);
	}
	return rc;
}

void dsdpcm_wrapper_t::free() {
	if (m_use_playback) {
		s_playback.free();
	}
	else {
		m_convert.free();
	}
}

size_t dsdpcm_wrapper_t::convert(const unsigned char* p_dsd_data, size_t p_dsd_size, audio_sample* p_pcm_data) {
	size_t pcm_size;
	if (m_use_playback) {
		pcm_size = s_playback.convert(p_dsd_data, p_dsd_size, p_pcm_data);
		if (s_use_correction) {
			correct(p_pcm_data, pcm_size, s_channels, s_playback.get_delay());
			s_use_correction = false;
		}
		if (s_ramp_timedown > 0.0) {
			for (auto sample = 0u; sample < pcm_size / s_channels; sample++) {
				auto volume =	static_cast<audio_sample>((s_ramp_timedown > s_ramp_duration - s_ramp_delay) ? 0.0 : ((s_ramp_timedown > 0.0) ? (s_ramp_duration - s_ramp_delay - s_ramp_timedown) / (s_ramp_duration - s_ramp_delay) : 1.0));
				s_ramp_timedown -= 1.0 / m_decoder_samplerate;
				for (auto ch = 0u; ch < s_channels; ch++) {
					p_pcm_data[sample * s_channels + ch] *= volume;
				}
			}
		}
	}
	else {
		pcm_size = m_convert.convert(p_dsd_data, p_dsd_size, p_pcm_data);
	}
	return pcm_size;
}

void dsdpcm_wrapper_t::correct(audio_sample* p_pcm_data, size_t p_pcm_size, size_t p_channels, double p_delay) {
	auto samples = std::min(p_channels ? p_pcm_size / p_channels : 0u, size_t(2.0 * p_delay));
	auto zeroes = 2 * samples / 3;
	for (auto sample = 0u; sample < samples; sample++) {
		auto scale = (sample < zeroes) ? audio_sample(0) : audio_sample(sample - zeroes) / audio_sample(samples - zeroes);
		for (auto ch = 0u; ch < p_channels; ch++) {
			p_pcm_data[sample * p_channels + ch] *= scale;
		}
	}
}
