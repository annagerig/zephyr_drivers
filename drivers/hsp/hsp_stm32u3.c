/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32U3 HSP hardware FFT, built like drivers/crc/crc_stm32.c. Uses Zephyr's
 * ST HAL (HAL_HSP_*); from ST only the firmware blob and command ids in st/.
 */

#define DT_DRV_COMPAT st_stm32u3_hsp

#include <errno.h>
#include <string.h>

#include <soc.h>
#include <stm32u3xx_hal.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/logging/log.h>

#include <vt1/drivers/hsp.h>
#include "st/hsp_fw_ram.h"
#include "st/hsp_fw_def.h"

LOG_MODULE_REGISTER(hsp_stm32u3, CONFIG_HSP_STM32_LOG_LEVEL);

#define HSP_DRV_BOOT_TIMEOUT_MS 500U
#define HSP_DRV_CMD_TIMEOUT_MS  2000U

struct hsp_stm32u3_config {
	HSP_TypeDef *base;
	struct stm32_pclken pclken[1];
};

struct hsp_stm32u3_data {
	HSP_HandleTypeDef h;
	float *bram;          /* BRAM-AB working buffer (HAL-reported) */
	uint32_t bram_words;
};

/* fft_attr field the HSP RFFT expects (see ST get_fft_attr): bit-reverse adds
 * log2, inverse-FFT sets bit 5. We only ever do a forward, bit-reversed RFFT. */
static inline uint32_t hsp_fft_attr(uint32_t log2, uint32_t bitrev, uint32_t ifft)
{
	uint32_t attr = 0U;

	if (bitrev) {
		attr += log2;
	}
	if (ifft) {
		attr += 0x20U;
	}
	return attr;
}

static int hsp_stm32u3_rfft_f32(const struct device *dev, const float *in,
				float *out, uint8_t log2n)
{
	struct hsp_stm32u3_data *data = dev->data;
	const uint32_t n = 1U << log2n;

	if (n > data->bram_words) {
		return -EINVAL;
	}

	/* In-place RFFT in BRAM, TYPE_2 = CMSIS packed output, synchronous
	 * direct command as ST's HSP_ACC_Rfft_f32. */
	memcpy(data->bram, in, n * sizeof(float));

	HAL_HSP_WriteParameter(&data->h, HAL_HSP_PARAM_2,
			       hsp_fft_attr(log2n - 1U, 1U, 0U));
	HAL_HSP_WriteParameter(&data->h, HAL_HSP_PARAM_3, 1U << (log2n - 1U));
	HAL_HSP_WriteParameter(&data->h, HAL_HSP_PARAM_4, (uint32_t)(log2n - 1U));
	HAL_HSP_WriteParameter(&data->h, HAL_HSP_PARAM_5, 0U);          /* forward */
	HAL_HSP_WriteParameter(&data->h, HAL_HSP_PARAM_6, HSP_TYPE_RFFT_TYPE_2);
	HAL_HSP_DIRECT_WriteDataPointer(&data->h,
				(HAL_HSP_DIRECT_CMD_Data_PtrTypeDef)0U,
				(uint32_t)(uintptr_t)data->bram);
	HAL_HSP_DIRECT_WriteCommand(&data->h, HSP_DIRECT_CMD_RFFT_F32);

	if (HAL_HSP_DIRECT_WaitEndOfCommand(&data->h, HSP_DRV_CMD_TIMEOUT_MS)
	    != HAL_OK) {
		LOG_ERR("RFFT timed out");
		return -EIO;
	}

	memcpy(out, data->bram, n * sizeof(float));
	return 0;
}

static DEVICE_API(hsp, hsp_stm32u3_api) = {
	.rfft_f32 = hsp_stm32u3_rfft_f32,
};

static int hsp_stm32u3_init(const struct device *dev)
{
	const struct hsp_stm32u3_config *cfg = dev->config;
	struct hsp_stm32u3_data *data = dev->data;
	const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	const hsp_fw_descriptor_t *cram = hsp_fw_getcram();
	const hsp_fw_descriptor_t *dram = hsp_fw_getdram();
	HAL_HSP_CapabilitiesTypeDef capa;
	const HAL_HSP_Boot_ConfigTypeDef boot = {
		.boot_cmd_id = HSP_CMD_FW_START,
		.boot_success_code = HSP_BSTAT_BOOTOK,
		.perf_mon = HAL_HSP_PERF_MONITOR_DISABLE,
		.perf_counter_offset = HSP_DRAM_CC_ADDR,
	};
	int ret;

	/* The HSP needs Range 1, which the 96 MHz core clock already implies. */
	ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken[0]);
	if (ret != 0) {
		LOG_ERR("clock on failed: %d", ret);
		return ret;
	}

	data->h.Instance = cfg->base;
	if (HAL_HSP_Init(&data->h) != HAL_OK) {
		LOG_ERR("HAL_HSP_Init failed");
		return -EIO;
	}
	if (HAL_HSP_LoadFirmware(&data->h, cram->p_binary, cram->size_byte,
				 dram->p_binary, dram->size_byte) != HAL_OK) {
		LOG_ERR("LoadFirmware failed");
		return -EIO;
	}
	if (HAL_HSP_Boot(&data->h, &boot, HSP_DRV_BOOT_TIMEOUT_MS) != HAL_OK) {
		LOG_ERR("Boot failed");
		return -EIO;
	}
	if (HAL_HSP_GetCapabilities(&data->h, &capa) != HAL_OK) {
		LOG_ERR("GetCapabilities failed");
		return -EIO;
	}

	data->bram = (float *)(uintptr_t)capa.bramab_base_address;
	data->bram_words = capa.bramab_size_byte / sizeof(float);
	LOG_INF("HSP ready: BRAM %u B @ 0x%08x (%u words)",
		capa.bramab_size_byte, capa.bramab_base_address, data->bram_words);
	return 0;
}

#define HSP_STM32U3_INIT(inst)							\
	static const struct hsp_stm32u3_config hsp_stm32u3_config_##inst = {	\
		.base = (HSP_TypeDef *)DT_INST_REG_ADDR(inst),			\
		.pclken = STM32_DT_INST_CLOCKS(inst),				\
	};									\
	static struct hsp_stm32u3_data hsp_stm32u3_data_##inst;			\
	DEVICE_DT_INST_DEFINE(inst, hsp_stm32u3_init, NULL,			\
			      &hsp_stm32u3_data_##inst,				\
			      &hsp_stm32u3_config_##inst,			\
			      POST_KERNEL, CONFIG_HSP_STM32_INIT_PRIORITY,	\
			      &hsp_stm32u3_api);

DT_INST_FOREACH_STATUS_OKAY(HSP_STM32U3_INIT)
