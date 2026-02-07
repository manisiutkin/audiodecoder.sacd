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

#include "../../lib/libdsdpcm/binding/dsdpcm_decoder.h"
#include <kodi/AddonBase.h>

class ATTR_DLL_LOCAL dsdpcm_wrapper_t final {
	static dsdpcm_decoder_t s_playback;
	static size_t s_channels;
	static size_t s_framerate;
	static size_t s_dsd_samplerate;
	static size_t s_pcm_samplerate;
	static bool s_req_init;
	static bool s_use_ramp;
	static bool s_use_correction;
	static double s_ramp_duration;
	static double s_ramp_delay;
	static double s_ramp_timedown;
 	dsdpcm_decoder_t m_convert;
	bool m_use_playback{ false };
	size_t m_decoder_samplerate{ 0 };
public:
	static void req_init();
	static bool use_ramp();
	static void set_ramp(bool p_use);
	static void set_ramp(double p_duration);
	void set_payback(bool p_use);
	double get_delay();
	size_t get_decoder_samplerate();
	int init(size_t p_channels, size_t p_framerate, size_t p_dsd_samplerate, size_t p_pcm_samplerate, conv_type_e p_conv_type, bool p_conv_fp64, double* p_fir_data = nullptr, size_t p_fir_size = 0, size_t p_fir_decimation = 0);
	void free();
	size_t convert(const unsigned char* p_dsd_data, size_t p_dsd_size, audio_sample* p_pcm_data);
	void correct(audio_sample* p_pcm_data, size_t p_pcm_size, size_t p_channels, double p_delay);
};
