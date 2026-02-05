/*
* SACD Decoder plugin
* Copyright (c) 2011-2025 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with FFmpeg; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include <type_traits>
#include <arm_neon.h>

template<typename real_t>
class base_fir_t {
protected:
	typedef std::conditional_t<std::is_same_v<real_t, float>, float32x4_t, float64x2_t> vreal_t;
	static constexpr auto neon_alignment = sizeof(vreal_t) / sizeof(real_t);
	inline static vreal_t vdupq_n_real(real_t value) {
		if constexpr (std::is_same_v<real_t, float>) {
			return vdupq_n_f32(value);
		}
		else {
			return vdupq_n_f64(value);
		}
	}
	inline static vreal_t vld1q_real(real_t* values) {
		if constexpr (std::is_same_v<real_t, float>) {
			return vld1q_f32(values);
		}
		else {
			return vld1q_f64(values);
		}
	}
	inline static vreal_t vld1q_real(real_t* values, size_t increment) {
		if constexpr (std::is_same_v<real_t, float>) {
			real_t pkt_values[4]{ values[0 * increment], values[1 * increment], values[2 * increment], values[3 * increment] };
			return vld1q_f32(pkt_values);
		}
		else {
			real_t pkt_values[2]{ values[0 * increment], values[1 * increment] };
			return vld1q_f64(pkt_values);
		}
	}
	inline static vreal_t vaddq_real(vreal_t v1, vreal_t v2) {
		if constexpr (std::is_same_v<real_t, float>) {
			return vaddq_f32(v1, v2);
		}
		else {
			return vaddq_f64(v1, v2);
		}
	}
	inline static vreal_t vmulq_real(vreal_t v1, vreal_t v2) {
		if constexpr (std::is_same_v<real_t, float>) {
			return vmulq_f32(v1, v2);
		}
		else {
			return vmulq_f64(v1, v2);
		}
	}
	inline static real_t vaddvq_real(vreal_t value) {
		if constexpr (std::is_same_v<real_t, float>) {
			return vaddvq_f32(value);
			//return value.n128_f32[0] + value.n128_f32[1] + value.n128_f32[2] + value.n128_f32[3];
		}
		else {
			return vaddvq_f64(value);
			//return value.n128_f64[0] + value.n128_f64[1];
		}
	}
};
