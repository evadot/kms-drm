/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Madhav Chauhan <madhav.chauhan@intel.com>
 *   Jani Nikula <jani.nikula@intel.com>
 */

#include "intel_dsi.h"

static enum transcoder dsi_port_to_transcoder(enum port port)
{
	if (port == PORT_A)
		return TRANSCODER_DSI_0;
	else
		return TRANSCODER_DSI_1;
}

static void dsi_program_swing_and_deemphasis(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;
	int lane;

	for_each_dsi_port(port, intel_dsi->ports) {

		/*
		 * Program voltage swing and pre-emphasis level values as per
		 * table in BSPEC under DDI buffer programing
		 */
		tmp = I915_READ(ICL_PORT_TX_DW5_LN0(port));
		tmp &= ~(SCALING_MODE_SEL_MASK | RTERM_SELECT_MASK);
		tmp |= SCALING_MODE_SEL(0x2);
		tmp |= TAP2_DISABLE | TAP3_DISABLE;
		tmp |= RTERM_SELECT(0x6);
		I915_WRITE(ICL_PORT_TX_DW5_GRP(port), tmp);

		tmp = I915_READ(ICL_PORT_TX_DW5_AUX(port));
		tmp &= ~(SCALING_MODE_SEL_MASK | RTERM_SELECT_MASK);
		tmp |= SCALING_MODE_SEL(0x2);
		tmp |= TAP2_DISABLE | TAP3_DISABLE;
		tmp |= RTERM_SELECT(0x6);
		I915_WRITE(ICL_PORT_TX_DW5_AUX(port), tmp);

		tmp = I915_READ(ICL_PORT_TX_DW2_LN0(port));
		tmp &= ~(SWING_SEL_LOWER_MASK | SWING_SEL_UPPER_MASK |
			 RCOMP_SCALAR_MASK);
		tmp |= SWING_SEL_UPPER(0x2);
		tmp |= SWING_SEL_LOWER(0x2);
		tmp |= RCOMP_SCALAR(0x98);
		I915_WRITE(ICL_PORT_TX_DW2_GRP(port), tmp);

		tmp = I915_READ(ICL_PORT_TX_DW2_AUX(port));
		tmp &= ~(SWING_SEL_LOWER_MASK | SWING_SEL_UPPER_MASK |
			 RCOMP_SCALAR_MASK);
		tmp |= SWING_SEL_UPPER(0x2);
		tmp |= SWING_SEL_LOWER(0x2);
		tmp |= RCOMP_SCALAR(0x98);
		I915_WRITE(ICL_PORT_TX_DW2_AUX(port), tmp);

		tmp = I915_READ(ICL_PORT_TX_DW4_AUX(port));
		tmp &= ~(POST_CURSOR_1_MASK | POST_CURSOR_2_MASK |
			 CURSOR_COEFF_MASK);
		tmp |= POST_CURSOR_1(0x0);
		tmp |= POST_CURSOR_2(0x0);
		tmp |= CURSOR_COEFF(0x3f);
		I915_WRITE(ICL_PORT_TX_DW4_AUX(port), tmp);

		for (lane = 0; lane <= 3; lane++) {
			/* Bspec: must not use GRP register for write */
			tmp = I915_READ(ICL_PORT_TX_DW4_LN(port, lane));
			tmp &= ~(POST_CURSOR_1_MASK | POST_CURSOR_2_MASK |
				 CURSOR_COEFF_MASK);
			tmp |= POST_CURSOR_1(0x0);
			tmp |= POST_CURSOR_2(0x0);
			tmp |= CURSOR_COEFF(0x3f);
			I915_WRITE(ICL_PORT_TX_DW4_LN(port, lane), tmp);
		}
	}
}

static void gen11_dsi_program_esc_clk_div(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	u32 afe_clk_khz; /* 8X Clock */
	u32 esc_clk_div_m;

	afe_clk_khz = DIV_ROUND_CLOSEST(intel_dsi->pclk * bpp,
					intel_dsi->lane_count);

	esc_clk_div_m = DIV_ROUND_UP(afe_clk_khz, DSI_MAX_ESC_CLK);

	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(ICL_DSI_ESC_CLK_DIV(port),
			   esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		POSTING_READ(ICL_DSI_ESC_CLK_DIV(port));
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(ICL_DPHY_ESC_CLK_DIV(port),
			   esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		POSTING_READ(ICL_DPHY_ESC_CLK_DIV(port));
	}
}

static void gen11_dsi_enable_io_power(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_DSI_IO_MODECTL(port));
		tmp |= COMBO_PHY_MODE_DSI;
		I915_WRITE(ICL_DSI_IO_MODECTL(port), tmp);
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		intel_display_power_get(dev_priv, port == PORT_A ?
					POWER_DOMAIN_PORT_DDI_A_IO :
					POWER_DOMAIN_PORT_DDI_B_IO);
	}
}

static void gen11_dsi_power_up_lanes(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;
	u32 lane_mask;

	switch (intel_dsi->lane_count) {
	case 1:
		lane_mask = PWR_DOWN_LN_3_1_0;
		break;
	case 2:
		lane_mask = PWR_DOWN_LN_3_1;
		break;
	case 3:
		lane_mask = PWR_DOWN_LN_3;
		break;
	case 4:
	default:
		lane_mask = PWR_UP_ALL_LANES;
		break;
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_CL_DW10(port));
		tmp &= ~PWR_DOWN_LN_MASK;
		I915_WRITE(ICL_PORT_CL_DW10(port), tmp | lane_mask);
	}
}

static void gen11_dsi_config_phy_lanes_sequence(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;
	int lane;

	/* Step 4b(i) set loadgen select for transmit and aux lanes */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW4_AUX(port));
		tmp &= ~LOADGEN_SELECT;
		I915_WRITE(ICL_PORT_TX_DW4_AUX(port), tmp);
		for (lane = 0; lane <= 3; lane++) {
			tmp = I915_READ(ICL_PORT_TX_DW4_LN(port, lane));
			tmp &= ~LOADGEN_SELECT;
			if (lane != 2)
				tmp |= LOADGEN_SELECT;
			I915_WRITE(ICL_PORT_TX_DW4_LN(port, lane), tmp);
		}
	}

	/* Step 4b(ii) set latency optimization for transmit and aux lanes */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW2_AUX(port));
		tmp &= ~FRC_LATENCY_OPTIM_MASK;
		tmp |= FRC_LATENCY_OPTIM_VAL(0x5);
		I915_WRITE(ICL_PORT_TX_DW2_AUX(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW2_LN0(port));
		tmp &= ~FRC_LATENCY_OPTIM_MASK;
		tmp |= FRC_LATENCY_OPTIM_VAL(0x5);
		I915_WRITE(ICL_PORT_TX_DW2_GRP(port), tmp);
	}

}

static void gen11_dsi_voltage_swing_program_seq(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 tmp;
	enum port port;

	/* clear common keeper enable bit */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_PCS_DW1_LN0(port));
		tmp &= ~COMMON_KEEPER_EN;
		I915_WRITE(ICL_PORT_PCS_DW1_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_PCS_DW1_AUX(port));
		tmp &= ~COMMON_KEEPER_EN;
		I915_WRITE(ICL_PORT_PCS_DW1_AUX(port), tmp);
	}

	/*
	 * Set SUS Clock Config bitfield to 11b
	 * Note: loadgen select program is done
	 * as part of lane phy sequence configuration
	 */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_CL_DW5(port));
		tmp |= SUS_CLOCK_CONFIG;
		I915_WRITE(ICL_PORT_CL_DW5(port), tmp);
	}

	/* Clear training enable to change swing values */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW5_LN0(port));
		tmp &= ~TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW5_AUX(port));
		tmp &= ~TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_AUX(port), tmp);
	}

	/* Program swing and de-emphasis */
	dsi_program_swing_and_deemphasis(encoder);

	/* Set training enable to trigger update */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW5_LN0(port));
		tmp |= TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW5_AUX(port));
		tmp |= TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_AUX(port), tmp);
	}
}

static void gen11_dsi_enable_ddi_buffer(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 tmp;
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(DDI_BUF_CTL(port));
		tmp |= DDI_BUF_CTL_ENABLE;
		I915_WRITE(DDI_BUF_CTL(port), tmp);

		if (wait_for_us(!(I915_READ(DDI_BUF_CTL(port)) &
				  DDI_BUF_IS_IDLE),
				  500))
			DRM_ERROR("DDI port:%c buffer idle\n", port_name(port));
	}
}

static void gen11_dsi_setup_dphy_timings(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 tmp;
	enum port port;

	/* Program T-INIT master registers */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_DSI_T_INIT_MASTER(port));
		tmp &= ~MASTER_INIT_TIMER_MASK;
		tmp |= intel_dsi->init_count;
		I915_WRITE(ICL_DSI_T_INIT_MASTER(port), tmp);
	}

	/* Program DPHY clock lanes timings */
	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(DPHY_CLK_TIMING_PARAM(port), intel_dsi->dphy_reg);

		/* shadow register inside display core */
		I915_WRITE(DSI_CLK_TIMING_PARAM(port), intel_dsi->dphy_reg);
	}

	/* Program DPHY data lanes timings */
	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(DPHY_DATA_TIMING_PARAM(port),
			   intel_dsi->dphy_data_lane_reg);

		/* shadow register inside display core */
		I915_WRITE(DSI_DATA_TIMING_PARAM(port),
			   intel_dsi->dphy_data_lane_reg);
	}

	/*
	 * If DSI link operating at or below an 800 MHz,
	 * TA_SURE should be override and programmed to
	 * a value '0' inside TA_PARAM_REGISTERS otherwise
	 * leave all fields at HW default values.
	 */
	if (intel_dsi_bitrate(intel_dsi) <= 800000) {
		for_each_dsi_port(port, intel_dsi->ports) {
			tmp = I915_READ(DPHY_TA_TIMING_PARAM(port));
			tmp &= ~TA_SURE_MASK;
			tmp |= TA_SURE_OVERRIDE | TA_SURE(0);
			I915_WRITE(DPHY_TA_TIMING_PARAM(port), tmp);

			/* shadow register inside display core */
			tmp = I915_READ(DSI_TA_TIMING_PARAM(port));
			tmp &= ~TA_SURE_MASK;
			tmp |= TA_SURE_OVERRIDE | TA_SURE(0);
			I915_WRITE(DSI_TA_TIMING_PARAM(port), tmp);
		}
	}
}

static void
gen11_dsi_configure_transcoder(struct intel_encoder *encoder,
			       const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->base.crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 tmp;
	enum port port;
	enum transcoder dsi_trans;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = I915_READ(DSI_TRANS_FUNC_CONF(dsi_trans));

		if (intel_dsi->eotp_pkt)
			tmp &= ~EOTP_DISABLED;
		else
			tmp |= EOTP_DISABLED;

		/* enable link calibration if freq > 1.5Gbps */
		if (intel_dsi_bitrate(intel_dsi) >= 1500 * 1000) {
			tmp &= ~LINK_CALIBRATION_MASK;
			tmp |= CALIBRATION_ENABLED_INITIAL_ONLY;
		}

		/* configure continuous clock */
		tmp &= ~CONTINUOUS_CLK_MASK;
		if (intel_dsi->clock_stop)
			tmp |= CLK_ENTER_LP_AFTER_DATA;
		else
			tmp |= CLK_HS_CONTINUOUS;

		/* configure buffer threshold limit to minimum */
		tmp &= ~PIX_BUF_THRESHOLD_MASK;
		tmp |= PIX_BUF_THRESHOLD_1_4;

		/* set virtual channel to '0' */
		tmp &= ~PIX_VIRT_CHAN_MASK;
		tmp |= PIX_VIRT_CHAN(0);

		/* program BGR transmission */
		if (intel_dsi->bgr_enabled)
			tmp |= BGR_TRANSMISSION;

		/* select pixel format */
		tmp &= ~PIX_FMT_MASK;
		switch (intel_dsi->pixel_format) {
		default:
			MISSING_CASE(intel_dsi->pixel_format);
			/* fallthrough */
		case MIPI_DSI_FMT_RGB565:
			tmp |= PIX_FMT_RGB565;
			break;
		case MIPI_DSI_FMT_RGB666_PACKED:
			tmp |= PIX_FMT_RGB666_PACKED;
			break;
		case MIPI_DSI_FMT_RGB666:
			tmp |= PIX_FMT_RGB666_LOOSE;
			break;
		case MIPI_DSI_FMT_RGB888:
			tmp |= PIX_FMT_RGB888;
			break;
		}

		/* program DSI operation mode */
		if (is_vid_mode(intel_dsi)) {
			tmp &= ~OP_MODE_MASK;
			switch (intel_dsi->video_mode_format) {
			default:
				MISSING_CASE(intel_dsi->video_mode_format);
				/* fallthrough */
			case VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS:
				tmp |= VIDEO_MODE_SYNC_EVENT;
				break;
			case VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE:
				tmp |= VIDEO_MODE_SYNC_PULSE;
				break;
			}
		}

		I915_WRITE(DSI_TRANS_FUNC_CONF(dsi_trans), tmp);
	}

	/* enable port sync mode if dual link */
	if (intel_dsi->dual_link) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			tmp = I915_READ(TRANS_DDI_FUNC_CTL2(dsi_trans));
			tmp |= PORT_SYNC_MODE_ENABLE;
			I915_WRITE(TRANS_DDI_FUNC_CTL2(dsi_trans), tmp);
		}

		//TODO: configure DSS_CTL1
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);

		/* select data lane width */
		tmp = I915_READ(TRANS_DDI_FUNC_CTL(dsi_trans));
		tmp &= ~DDI_PORT_WIDTH_MASK;
		tmp |= DDI_PORT_WIDTH(intel_dsi->lane_count);

		/* select input pipe */
		tmp &= ~TRANS_DDI_EDP_INPUT_MASK;
		switch (pipe) {
		default:
			MISSING_CASE(pipe);
			/* fallthrough */
		case PIPE_A:
			tmp |= TRANS_DDI_EDP_INPUT_A_ON;
			break;
		case PIPE_B:
			tmp |= TRANS_DDI_EDP_INPUT_B_ONOFF;
			break;
		case PIPE_C:
			tmp |= TRANS_DDI_EDP_INPUT_C_ONOFF;
			break;
		}

		/* enable DDI buffer */
		tmp |= TRANS_DDI_FUNC_ENABLE;
		I915_WRITE(TRANS_DDI_FUNC_CTL(dsi_trans), tmp);
	}

	/* wait for link ready */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		if (wait_for_us((I915_READ(DSI_TRANS_FUNC_CONF(dsi_trans)) &
				LINK_READY), 2500))
			DRM_ERROR("DSI link not ready\n");
	}
}

static void
gen11_dsi_enable_port_and_phy(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config)
{
	/* step 4a: power up all lanes of the DDI used by DSI */
	gen11_dsi_power_up_lanes(encoder);

	/* step 4b: configure lane sequencing of the Combo-PHY transmitters */
	gen11_dsi_config_phy_lanes_sequence(encoder);

	/* step 4c: configure voltage swing and skew */
	gen11_dsi_voltage_swing_program_seq(encoder);

	/* enable DDI buffer */
	gen11_dsi_enable_ddi_buffer(encoder);

	/* setup D-PHY timings */
	gen11_dsi_setup_dphy_timings(encoder);

	/* Step (4h, 4i, 4j, 4k): Configure transcoder */
	gen11_dsi_configure_transcoder(encoder, pipe_config);
}

static void __attribute__((unused))
gen11_dsi_pre_enable(struct intel_encoder *encoder,
		     const struct intel_crtc_state *pipe_config,
		     const struct drm_connector_state *conn_state)
{
	/* step2: enable IO power */
	gen11_dsi_enable_io_power(encoder);

	/* step3: enable DSI PLL */
	gen11_dsi_program_esc_clk_div(encoder);

	/* step4: enable DSI port and DPHY */
	gen11_dsi_enable_port_and_phy(encoder, pipe_config);
}
