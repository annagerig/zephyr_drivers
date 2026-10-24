/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32U3 HSP hardware FFT. Custom device API (no Zephyr DSP accelerator
 * subsystem exists), in the style of <zephyr/drivers/fpga.h>.
 */

#ifndef VT1_DRIVERS_HSP_H_
#define VT1_DRIVERS_HSP_H_

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*hsp_api_rfft_f32)(const struct device *dev, const float *in,
				float *out, uint8_t log2n);

__subsystem struct hsp_driver_api {
	hsp_api_rfft_f32 rfft_f32;
};

/*
 * Real FFT of 2^log2n points. out uses the CMSIS packed layout (out[0] = DC,
 * out[1] = Nyquist, then re/im pairs) and may alias in. 0 or negative errno.
 */
static inline int hsp_rfft_f32(const struct device *dev, const float *in,
			       float *out, uint8_t log2n)
{
	const struct hsp_driver_api *api = DEVICE_API_GET(hsp, dev);

	if (api->rfft_f32 == NULL) {
		return -ENOSYS;
	}
	return api->rfft_f32(dev, in, out, log2n);
}

#ifdef __cplusplus
}
#endif

#endif /* VT1_DRIVERS_HSP_H_ */
