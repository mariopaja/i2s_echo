/*
 * Copyright (c) 2023 ZAL Zentrum für Angewandte Luftfahrtforschung GmbH
 * Copyright (c) 2023 Mario Paja
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/i2s.h>
#include <zephyr/audio/codec.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>

#include "math.h"

const struct device *const i2s_dev_rx = DEVICE_DT_GET(DT_NODELABEL(i2s_rx));
const struct device *const i2s_dev_tx = DEVICE_DT_GET(DT_NODELABEL(i2s_tx));

#ifdef CONFIG_AUDIO_CODEC
const struct device *const codec_dev = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
#endif

#define SAMPLE_FREQUENCY   (44100)
#define SAMPLE_BIT_WIDTH   (16)
#define NUMBER_OF_CHANNELS (2U)
#define SAMPLES_PER_BLOCK  (256 * NUMBER_OF_CHANNELS)
#define INITIAL_BLOCKS     4
#define TIMEOUT            (1000)

#define NUM_BLOCKS INITIAL_BLOCKS * 2

#define BLOCK_SIZE (SAMPLES_PER_BLOCK * sizeof(int16_t))

static char __nocache __aligned(WB_UP(32)) _rxtx_mem_slab_buff[(NUM_BLOCKS)*WB_UP(BLOCK_SIZE)];
static char __nocache __aligned(WB_UP(32)) _rx_mem_slab_buff[(NUM_BLOCKS)*WB_UP(BLOCK_SIZE)];
static char __nocache __aligned(WB_UP(32)) _tx_mem_slab_buff[(NUM_BLOCKS)*WB_UP(BLOCK_SIZE)];

static STRUCT_SECTION_ITERABLE(k_mem_slab, rxtx_mem_slab) =
	Z_MEM_SLAB_INITIALIZER(rxtx_mem_slab, _rxtx_mem_slab_buff, WB_UP(BLOCK_SIZE), NUM_BLOCKS);

static STRUCT_SECTION_ITERABLE(k_mem_slab,
			       rx_mem_slab) = Z_MEM_SLAB_INITIALIZER(rx_mem_slab, _rx_mem_slab_buff,
								     WB_UP(BLOCK_SIZE), NUM_BLOCKS);

static STRUCT_SECTION_ITERABLE(k_mem_slab,
			       tx_mem_slab) = Z_MEM_SLAB_INITIALIZER(tx_mem_slab, _tx_mem_slab_buff,
								     WB_UP(BLOCK_SIZE), NUM_BLOCKS);

#define I2S_FORMAT I2S_FMT_DATA_FORMAT_I2S

static struct i2s_config i2s_rx_cfg;
static struct i2s_config i2s_tx_cfg;

static int configure_i2s_stream(const struct device *dev, enum i2s_dir dir, i2s_opt_t options,
				struct i2s_config *cfg)
{
	int ret;

	cfg->word_size = SAMPLE_BIT_WIDTH;
	cfg->channels = NUMBER_OF_CHANNELS;
	cfg->format = I2S_FORMAT;
	cfg->options = options;
	cfg->frame_clk_freq = SAMPLE_FREQUENCY;
	cfg->block_size = BLOCK_SIZE;

	cfg->timeout = TIMEOUT;

	// Separate Slabs
	if (dir == I2S_DIR_TX) {
		cfg->mem_slab = &tx_mem_slab;
	} else {
		cfg->mem_slab = &rx_mem_slab;
	}

	ret = i2s_configure(dev, dir, cfg);
	if (ret != 0) {
		LOG_ERR("i2s_configure failed for dir %d with %d error", dir, ret);
	}

	return ret;
}

#define I2S_CONTROLLER I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER
#define I2S_TARGET     I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET

int main(void)
{
	int ret;

	/* configure i2s for audio playback */
	configure_i2s_stream(i2s_dev_tx, I2S_DIR_TX, I2S_CONTROLLER, &i2s_tx_cfg);

	/* configure i2s for audio record */
	configure_i2s_stream(i2s_dev_rx, I2S_DIR_RX, I2S_TARGET, &i2s_rx_cfg);

	k_msleep(100);

#ifdef CONFIG_AUDIO_CODEC
	struct audio_codec_cfg audio_cfg;
	audio_cfg.dai_route = AUDIO_ROUTE_PLAYBACK_CAPTURE;
	audio_cfg.dai_type = AUDIO_DAI_TYPE_I2S;
	audio_cfg.dai_cfg.i2s.word_size = SAMPLE_BIT_WIDTH;
	audio_cfg.dai_cfg.i2s.channels = NUMBER_OF_CHANNELS;
	audio_cfg.dai_cfg.i2s.format = I2S_FORMAT;
	audio_cfg.dai_cfg.i2s.options = I2S_OPT_FRAME_CLK_TARGET;
	audio_cfg.dai_cfg.i2s.frame_clk_freq = SAMPLE_FREQUENCY;
	audio_cfg.dai_cfg.i2s.block_size = BLOCK_SIZE;
	audio_cfg.mclk_freq = (SAMPLE_FREQUENCY * 256);

	audio_codec_configure(codec_dev, &audio_cfg);

#endif

	for (int i = 0; i < 4; ++i) {
		void *mem_block;
		ret = k_mem_slab_alloc(&tx_mem_slab, &mem_block, K_NO_WAIT);
		memset(mem_block, 0, BLOCK_SIZE);
		ret = i2s_write(i2s_dev_tx, mem_block, BLOCK_SIZE);
	}

	ret = i2s_trigger(i2s_dev_rx, I2S_DIR_RX, I2S_TRIGGER_START);
	if (ret != 0) {
		LOG_ERR("i2s_trigger RX failed with %d error\n", ret);
	}

	ret = i2s_trigger(i2s_dev_tx, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0) {
		LOG_ERR("i2s_trigger TX failed with %d error", ret);
		return ret;
	} 


	while (true) {
		uint32_t block_size;
		void *rx_block;
		void *tx_block;

		ret = i2s_read(i2s_dev_rx, &rx_block, &block_size);
		if (ret != 0) {
			LOG_ERR("i2s_read failed with %d error", ret);
			continue;
		}

		ret = k_mem_slab_alloc(&tx_mem_slab, &tx_block, K_NO_WAIT);
		if (ret != 0) {
			LOG_ERR("k_mem_slab_alloc failed with %d error", ret);
			k_mem_slab_free(&rx_mem_slab, rx_block);
			continue;
		}

		memcpy(tx_block, rx_block, block_size);
		k_mem_slab_free(&rx_mem_slab, rx_block);

		ret = i2s_write(i2s_dev_tx, tx_block, block_size);
		if (ret != 0) {
			LOG_ERR("i2s_write failed with %d error", ret);
		}
	}


	return 0;
}
