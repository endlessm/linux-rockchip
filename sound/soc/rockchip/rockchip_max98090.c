/*
 * Rockchip machine ASoC driver for boards using a MAX90809 CODEC.
 *
 * Copyright (c) 2014, ROCKCHIP CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/hdmi-codec.h>
#include <linux/hdmi-notifier.h>

#include "rockchip_i2s.h"
#include "../codecs/ts3a227e.h"

#define DRV_NAME "rockchip-snd-max98090"

static struct snd_soc_jack headset_jack;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin headset_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},

};

static struct snd_soc_jack_pin hdmi_jack_pin[] = {
	{
		.pin = "HDMI",
		.mask = SND_JACK_LINEOUT,
	},
};

static const struct snd_soc_dapm_widget rk_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_LINE("HDMI", NULL),
};

static const struct snd_soc_dapm_route rk_audio_map[] = {
	{"IN34", NULL, "Headset Mic"},
	{"IN34", NULL, "MICBIAS"},
	{"Headset Mic", NULL, "MICBIAS"},
	{"DMICL", NULL, "Int Mic"},
	{"Headphone", NULL, "HPL"},
	{"Headphone", NULL, "HPR"},
	{"Speaker", NULL, "SPKL"},
	{"Speaker", NULL, "SPKR"},
};

static const struct snd_kcontrol_new rk_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("HDMI"),
};

static int rk_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int mclk;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
		mclk = 12288000;
		break;
	case 44100:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set codec clock %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set codec clock %d\n", ret);
		return ret;
	}

	return ret;
}

static struct snd_soc_jack hdmi_card_jack;

static int rk_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_codec *codec = runtime->codec_dais[1]->codec;
	int ret;

	/* enable jack detection */
	ret = snd_soc_card_jack_new(card, "HDMI", SND_JACK_LINEOUT,
				    &hdmi_card_jack, hdmi_jack_pin,
				    ARRAY_SIZE(hdmi_jack_pin));
	if (ret) {
		dev_err(card->dev, "Can't create HDMI Jack %d\n", ret);
		return ret;
	}

	hdmi_codec_set_jack_detect(codec, &hdmi_card_jack);

	/* Enable Headset and 4 Buttons Jack detection */
	return snd_soc_card_jack_new(runtime->card, "Headset Jack",
			       SND_JACK_HEADSET |
			       SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			       SND_JACK_BTN_2 | SND_JACK_BTN_3,
			       &headset_jack,
			       headset_jack_pins,
			       ARRAY_SIZE(headset_jack_pins));
}

static int rk_98090_headset_init(struct snd_soc_component *component)
{
	return ts3a227e_enable_jack_detect(component, &headset_jack);
}

static struct snd_soc_ops rk_aif1_ops = {
	.hw_params = rk_aif1_hw_params,
};

static struct snd_soc_aux_dev rk_98090_headset_dev = {
	.name = "Headset Chip",
	.init = rk_98090_headset_init,
};

static struct snd_soc_dai_link_component rk_codecs[] = {
	{ },
	{
		.name = "hdmi-audio-codec.5.auto",
		.dai_name = "i2s-hifi",
	},
};

static struct snd_soc_dai_link rk_dailink = {
	.name = "Codecs",
	.stream_name = "Audio",
	.init = rk_init,
	.ops = &rk_aif1_ops,
	.codecs = rk_codecs,
	.num_codecs = ARRAY_SIZE(rk_codecs),
	/* set max98090 as slave */
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card snd_soc_card_rk = {
	.name = "ROCKCHIP-I2S",
	.owner = THIS_MODULE,
	.dai_link = &rk_dailink,
	.num_links = 1,
	.aux_dev = &rk_98090_headset_dev,
	.num_aux_devs = 1,
	.dapm_widgets = rk_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_dapm_widgets),
	.dapm_routes = rk_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_audio_map),
	.controls = rk_mc_controls,
	.num_controls = ARRAY_SIZE(rk_mc_controls),
};

static int snd_rk_mc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct snd_soc_card *card = &snd_soc_card_rk;
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args args;

	/* register the soc card */
	card->dev = &pdev->dev;

	rk_dailink.codecs[0].of_node = of_parse_phandle(np,
			"rockchip,audio-codec", 0);
	if (!rk_dailink.codecs[0].of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,audio-codec' missing or invalid\n");
		return -EINVAL;
	}

	ret = of_parse_phandle_with_fixed_args(np, "rockchip,audio-codec",
					       0, 0, &args);
	if (ret) {
		dev_err(&pdev->dev, "Unable to parse property 'rockchip,audio-codec'\n");
		return ret;
	}

	ret = snd_soc_get_dai_name(&args, &rk_dailink.codecs[0].dai_name);
	if (ret) {
		dev_err(&pdev->dev, "Unable to get codec_dai_name\n");
		return ret;
	}

	rk_dailink.cpu_of_node = of_parse_phandle(np,
			"rockchip,i2s-controller", 0);
	if (!rk_dailink.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,i2s-controller' missing or invalid\n");
		return -EINVAL;
	}

	rk_dailink.platform_of_node = rk_dailink.cpu_of_node;

	rk_98090_headset_dev.codec_of_node = of_parse_phandle(np,
			"rockchip,headset-codec", 0);
	if (!rk_98090_headset_dev.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,headset-codec' missing/invalid\n");
		return -EINVAL;
	}

	ret = snd_soc_of_parse_card_name(card, "rockchip,model");
	if (ret) {
		dev_err(&pdev->dev,
			"Soc parse card name failed %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
			"Soc register card failed %d\n", ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id rockchip_max98090_of_match[] = {
	{ .compatible = "rockchip,rockchip-audio-max98090", },
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_max98090_of_match);

static struct platform_driver snd_rk_mc_driver = {
	.probe = snd_rk_mc_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_max98090_of_match,
	},
};

module_platform_driver(snd_rk_mc_driver);

MODULE_AUTHOR("jianqun <jay.xu@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip max98090 machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
