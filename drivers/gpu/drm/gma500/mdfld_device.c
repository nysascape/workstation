// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/delay.h>
#include <linux/gpio/machine.h>

#include <asm/intel_scu_ipc.h>

#include "mdfld_dsi_output.h"
#include "mdfld_output.h"
#include "mid_bios.h"
#include "psb_drv.h"
#include "tc35876x-dsi-lvds.h"

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE

#define MRST_BLC_MAX_PWM_REG_FREQ	    0xFFFF
#define BLC_PWM_PRECISION_FACTOR 100	/* 10000000 */
#define BLC_PWM_FREQ_CALC_CONSTANT 32
#define MHz 1000000
#define BRIGHTNESS_MIN_LEVEL 1
#define BRIGHTNESS_MAX_LEVEL 100
#define BRIGHTNESS_MASK	0xFF
#define BLC_POLARITY_NORMAL 0
#define BLC_POLARITY_INVERSE 1
#define BLC_ADJUSTMENT_MAX 100

#define MDFLD_BLC_PWM_PRECISION_FACTOR    10
#define MDFLD_BLC_MAX_PWM_REG_FREQ        0xFFFE
#define MDFLD_BLC_MIN_PWM_REG_FREQ        0x2

#define MDFLD_BACKLIGHT_PWM_POLARITY_BIT_CLEAR (0xFFFE)
#define MDFLD_BACKLIGHT_PWM_CTL_SHIFT	(16)

static struct backlight_device *mdfld_backlight_device;

int mdfld_set_brightness(struct backlight_device *bd)
{
	struct drm_device *dev =
		(struct drm_device *)bl_get_data(mdfld_backlight_device);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int level = bd->props.brightness;

	DRM_DEBUG_DRIVER("backlight level set to %d\n", level);

	/* Perform value bounds checking */
	if (level < BRIGHTNESS_MIN_LEVEL)
		level = BRIGHTNESS_MIN_LEVEL;

	if (gma_power_begin(dev, false)) {
		u32 adjusted_level = 0;

		/*
		 * Adjust the backlight level with the percent in
		 * dev_priv->blc_adj2
		 */
		adjusted_level = level * dev_priv->blc_adj2;
		adjusted_level = adjusted_level / BLC_ADJUSTMENT_MAX;
		dev_priv->brightness_adjusted = adjusted_level;

		if (mdfld_get_panel_type(dev, 0) == TC35876X) {
			if (dev_priv->dpi_panel_on[0] ||
					dev_priv->dpi_panel_on[2])
				tc35876x_brightness_control(dev,
						dev_priv->brightness_adjusted);
		} else {
			if (dev_priv->dpi_panel_on[0])
				mdfld_dsi_brightness_control(dev, 0,
						dev_priv->brightness_adjusted);
		}

		if (dev_priv->dpi_panel_on[2])
			mdfld_dsi_brightness_control(dev, 2,
					dev_priv->brightness_adjusted);
		gma_power_end(dev);
	}

	/* cache the brightness for later use */
	dev_priv->brightness = level;
	return 0;
}

static int mdfld_get_brightness(struct backlight_device *bd)
{
	struct drm_device *dev =
		(struct drm_device *)bl_get_data(mdfld_backlight_device);
	struct drm_psb_private *dev_priv = dev->dev_private;

	DRM_DEBUG_DRIVER("brightness = 0x%x \n", dev_priv->brightness);

	/* return locally cached var instead of HW read (due to DPST etc.) */
	return dev_priv->brightness;
}

static const struct backlight_ops mdfld_ops = {
	.get_brightness = mdfld_get_brightness,
	.update_status  = mdfld_set_brightness,
};

static int device_backlight_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = (struct drm_psb_private *)
		dev->dev_private;

	dev_priv->blc_adj1 = BLC_ADJUSTMENT_MAX;
	dev_priv->blc_adj2 = BLC_ADJUSTMENT_MAX;

	return 0;
}

static int mdfld_backlight_init(struct drm_device *dev)
{
	struct backlight_properties props;
	int ret = 0;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = BRIGHTNESS_MAX_LEVEL;
	props.type = BACKLIGHT_PLATFORM;
	mdfld_backlight_device = backlight_device_register("mdfld-bl",
				NULL, (void *)dev, &mdfld_ops, &props);

	if (IS_ERR(mdfld_backlight_device))
		return PTR_ERR(mdfld_backlight_device);

	ret = device_backlight_init(dev);
	if (ret)
		return ret;

	mdfld_backlight_device->props.brightness = BRIGHTNESS_MAX_LEVEL;
	mdfld_backlight_device->props.max_brightness = BRIGHTNESS_MAX_LEVEL;
	backlight_update_status(mdfld_backlight_device);
	return 0;
}
#endif

struct backlight_device *mdfld_get_backlight_device(void)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	return mdfld_backlight_device;
#else
	return NULL;
#endif
}

/*
 * mdfld_save_display_registers
 *
 * Description: We are going to suspend so save current display
 * register state.
 *
 * Notes: FIXME_JLIU7 need to add the support for DPI MIPI & HDMI audio
 */
static int mdfld_save_display_registers(struct drm_device *dev, int pipenum)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct medfield_state *regs = &dev_priv->regs.mdfld;
	struct psb_pipe *pipe = &dev_priv->regs.pipe[pipenum];
	const struct psb_offset *map = &dev_priv->regmap[pipenum];
	int i;
	u32 *mipi_val;

	/* register */
	u32 mipi_reg = MIPI;

	switch (pipenum) {
	case 0:
		mipi_val = &regs->saveMIPI;
		break;
	case 1:
		mipi_val = &regs->saveMIPI;
		break;
	case 2:
		/* register */
		mipi_reg = MIPI_C;
		/* pointer to values */
		mipi_val = &regs->saveMIPI_C;
		break;
	default:
		DRM_ERROR("%s, invalid pipe number.\n", __func__);
		return -EINVAL;
	}

	/* Pipe & plane A info */
	pipe->dpll = PSB_RVDC32(map->dpll);
	pipe->fp0 = PSB_RVDC32(map->fp0);
	pipe->conf = PSB_RVDC32(map->conf);
	pipe->htotal = PSB_RVDC32(map->htotal);
	pipe->hblank = PSB_RVDC32(map->hblank);
	pipe->hsync = PSB_RVDC32(map->hsync);
	pipe->vtotal = PSB_RVDC32(map->vtotal);
	pipe->vblank = PSB_RVDC32(map->vblank);
	pipe->vsync = PSB_RVDC32(map->vsync);
	pipe->src = PSB_RVDC32(map->src);
	pipe->stride = PSB_RVDC32(map->stride);
	pipe->linoff = PSB_RVDC32(map->linoff);
	pipe->tileoff = PSB_RVDC32(map->tileoff);
	pipe->size = PSB_RVDC32(map->size);
	pipe->pos = PSB_RVDC32(map->pos);
	pipe->surf = PSB_RVDC32(map->surf);
	pipe->cntr = PSB_RVDC32(map->cntr);
	pipe->status = PSB_RVDC32(map->status);

	/*save palette (gamma) */
	for (i = 0; i < 256; i++)
		pipe->palette[i] = PSB_RVDC32(map->palette + (i << 2));

	if (pipenum == 1) {
		regs->savePFIT_CONTROL = PSB_RVDC32(PFIT_CONTROL);
		regs->savePFIT_PGM_RATIOS = PSB_RVDC32(PFIT_PGM_RATIOS);

		regs->saveHDMIPHYMISCCTL = PSB_RVDC32(HDMIPHYMISCCTL);
		regs->saveHDMIB_CONTROL = PSB_RVDC32(HDMIB_CONTROL);
		return 0;
	}

	*mipi_val = PSB_RVDC32(mipi_reg);
	return 0;
}

/*
 * mdfld_restore_display_registers
 *
 * Description: We are going to resume so restore display register state.
 *
 * Notes: FIXME_JLIU7 need to add the support for DPI MIPI & HDMI audio
 */
static int mdfld_restore_display_registers(struct drm_device *dev, int pipenum)
{
	/* To get  panel out of ULPS mode. */
	u32 temp = 0;
	u32 device_ready_reg = DEVICE_READY_REG;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mdfld_dsi_config *dsi_config = NULL;
	struct medfield_state *regs = &dev_priv->regs.mdfld;
	struct psb_pipe *pipe = &dev_priv->regs.pipe[pipenum];
	const struct psb_offset *map = &dev_priv->regmap[pipenum];
	u32 i;
	u32 dpll;
	u32 timeout = 0;

	/* register */
	u32 mipi_reg = MIPI;

	/* values */
	u32 dpll_val = pipe->dpll;
	u32 mipi_val = regs->saveMIPI;

	switch (pipenum) {
	case 0:
		dpll_val &= ~DPLL_VCO_ENABLE;
		dsi_config = dev_priv->dsi_configs[0];
		break;
	case 1:
		dpll_val &= ~DPLL_VCO_ENABLE;
		break;
	case 2:
		mipi_reg = MIPI_C;
		mipi_val = regs->saveMIPI_C;
		dsi_config = dev_priv->dsi_configs[1];
		break;
	default:
		DRM_ERROR("%s, invalid pipe number.\n", __func__);
		return -EINVAL;
	}

	/*make sure VGA plane is off. it initializes to on after reset!*/
	PSB_WVDC32(0x80000000, VGACNTRL);

	if (pipenum == 1) {
		PSB_WVDC32(dpll_val & ~DPLL_VCO_ENABLE, map->dpll);
		PSB_RVDC32(map->dpll);

		PSB_WVDC32(pipe->fp0, map->fp0);
	} else {

		dpll = PSB_RVDC32(map->dpll);

		if (!(dpll & DPLL_VCO_ENABLE)) {

			/* When ungating power of DPLL, needs to wait 0.5us
			   before enable the VCO */
			if (dpll & MDFLD_PWR_GATE_EN) {
				dpll &= ~MDFLD_PWR_GATE_EN;
				PSB_WVDC32(dpll, map->dpll);
				/* FIXME_MDFLD PO - change 500 to 1 after PO */
				udelay(500);
			}

			PSB_WVDC32(pipe->fp0, map->fp0);
			PSB_WVDC32(dpll_val, map->dpll);
			/* FIXME_MDFLD PO - change 500 to 1 after PO */
			udelay(500);

			dpll_val |= DPLL_VCO_ENABLE;
			PSB_WVDC32(dpll_val, map->dpll);
			PSB_RVDC32(map->dpll);

			/* wait for DSI PLL to lock */
			while (timeout < 20000 &&
			  !(PSB_RVDC32(map->conf) & PIPECONF_DSIPLL_LOCK)) {
				udelay(150);
				timeout++;
			}

			if (timeout == 20000) {
				DRM_ERROR("%s, can't lock DSIPLL.\n",
								__func__);
				return -EINVAL;
			}
		}
	}
	/* Restore mode */
	PSB_WVDC32(pipe->htotal, map->htotal);
	PSB_WVDC32(pipe->hblank, map->hblank);
	PSB_WVDC32(pipe->hsync, map->hsync);
	PSB_WVDC32(pipe->vtotal, map->vtotal);
	PSB_WVDC32(pipe->vblank, map->vblank);
	PSB_WVDC32(pipe->vsync, map->vsync);
	PSB_WVDC32(pipe->src, map->src);
	PSB_WVDC32(pipe->status, map->status);

	/*set up the plane*/
	PSB_WVDC32(pipe->stride, map->stride);
	PSB_WVDC32(pipe->linoff, map->linoff);
	PSB_WVDC32(pipe->tileoff, map->tileoff);
	PSB_WVDC32(pipe->size, map->size);
	PSB_WVDC32(pipe->pos, map->pos);
	PSB_WVDC32(pipe->surf, map->surf);

	if (pipenum == 1) {
		/* restore palette (gamma) */
		/* udelay(50000); */
		for (i = 0; i < 256; i++)
			PSB_WVDC32(pipe->palette[i], map->palette + (i << 2));

		PSB_WVDC32(regs->savePFIT_CONTROL, PFIT_CONTROL);
		PSB_WVDC32(regs->savePFIT_PGM_RATIOS, PFIT_PGM_RATIOS);

		/*TODO: resume HDMI port */

		/*TODO: resume pipe*/

		/*enable the plane*/
		PSB_WVDC32(pipe->cntr & ~DISPLAY_PLANE_ENABLE, map->cntr);

		return 0;
	}

	/*set up pipe related registers*/
	PSB_WVDC32(mipi_val, mipi_reg);

	/*setup MIPI adapter + MIPI IP registers*/
	if (dsi_config)
		mdfld_dsi_controller_init(dsi_config, pipenum);

	if (in_atomic() || in_interrupt())
		mdelay(20);
	else
		msleep(20);

	/*enable the plane*/
	PSB_WVDC32(pipe->cntr, map->cntr);

	if (in_atomic() || in_interrupt())
		mdelay(20);
	else
		msleep(20);

	/* LP Hold Release */
	temp = REG_READ(mipi_reg);
	temp |= LP_OUTPUT_HOLD_RELEASE;
	REG_WRITE(mipi_reg, temp);
	mdelay(1);


	/* Set DSI host to exit from Utra Low Power State */
	temp = REG_READ(device_ready_reg);
	temp &= ~ULPS_MASK;
	temp |= 0x3;
	temp |= EXIT_ULPS_DEV_READY;
	REG_WRITE(device_ready_reg, temp);
	mdelay(1);

	temp = REG_READ(device_ready_reg);
	temp &= ~ULPS_MASK;
	temp |= EXITING_ULPS;
	REG_WRITE(device_ready_reg, temp);
	mdelay(1);

	/*enable the pipe*/
	PSB_WVDC32(pipe->conf, map->conf);

	/* restore palette (gamma) */
	/* udelay(50000); */
	for (i = 0; i < 256; i++)
		PSB_WVDC32(pipe->palette[i], map->palette + (i << 2));

	return 0;
}

static int mdfld_save_registers(struct drm_device *dev)
{
	/* mdfld_save_cursor_overlay_registers(dev); */
	mdfld_save_display_registers(dev, 0);
	mdfld_save_display_registers(dev, 2);
	mdfld_disable_crtc(dev, 0);
	mdfld_disable_crtc(dev, 2);

	return 0;
}

static int mdfld_restore_registers(struct drm_device *dev)
{
	mdfld_restore_display_registers(dev, 2);
	mdfld_restore_display_registers(dev, 0);
	/* mdfld_restore_cursor_overlay_registers(dev); */

	return 0;
}

static int mdfld_power_down(struct drm_device *dev)
{
	/* FIXME */
	return 0;
}

static int mdfld_power_up(struct drm_device *dev)
{
	/* FIXME */
	return 0;
}

/* Medfield  */
static const struct psb_offset mdfld_regmap[3] = {
	{
		.fp0 = MRST_FPA0,
		.fp1 = MRST_FPA1,
		.cntr = DSPACNTR,
		.conf = PIPEACONF,
		.src = PIPEASRC,
		.dpll = MRST_DPLL_A,
		.htotal = HTOTAL_A,
		.hblank = HBLANK_A,
		.hsync = HSYNC_A,
		.vtotal = VTOTAL_A,
		.vblank = VBLANK_A,
		.vsync = VSYNC_A,
		.stride = DSPASTRIDE,
		.size = DSPASIZE,
		.pos = DSPAPOS,
		.surf = DSPASURF,
		.addr = MRST_DSPABASE,
		.status = PIPEASTAT,
		.linoff = DSPALINOFF,
		.tileoff = DSPATILEOFF,
		.palette = PALETTE_A,
	},
	{
		.fp0 = MDFLD_DPLL_DIV0,
		.cntr = DSPBCNTR,
		.conf = PIPEBCONF,
		.src = PIPEBSRC,
		.dpll = MDFLD_DPLL_B,
		.htotal = HTOTAL_B,
		.hblank = HBLANK_B,
		.hsync = HSYNC_B,
		.vtotal = VTOTAL_B,
		.vblank = VBLANK_B,
		.vsync = VSYNC_B,
		.stride = DSPBSTRIDE,
		.size = DSPBSIZE,
		.pos = DSPBPOS,
		.surf = DSPBSURF,
		.addr = MRST_DSPBBASE,
		.status = PIPEBSTAT,
		.linoff = DSPBLINOFF,
		.tileoff = DSPBTILEOFF,
		.palette = PALETTE_B,
	},
	{
		.fp0 = MRST_FPA0,	/* This is what the old code did ?? */
		.cntr = DSPCCNTR,
		.conf = PIPECCONF,
		.src = PIPECSRC,
		/* No DPLL_C */
		.dpll = MRST_DPLL_A,
		.htotal = HTOTAL_C,
		.hblank = HBLANK_C,
		.hsync = HSYNC_C,
		.vtotal = VTOTAL_C,
		.vblank = VBLANK_C,
		.vsync = VSYNC_C,
		.stride = DSPCSTRIDE,
		.size = DSPBSIZE,
		.pos = DSPCPOS,
		.surf = DSPCSURF,
		.addr = MDFLD_DSPCBASE,
		.status = PIPECSTAT,
		.linoff = DSPCLINOFF,
		.tileoff = DSPCTILEOFF,
		.palette = PALETTE_C,
	},
};

/*
 * The GPIO lines for resetting DSI pipe 0 and 2 are available in the
 * PCI device 0000:00:0c.0 on the Medfield.
 */
static struct gpiod_lookup_table mdfld_dsi_pipe_gpio_table = {
	.table  = {
		GPIO_LOOKUP("0000:00:0c.0", 128, "dsi-pipe0-reset",
			    GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("0000:00:0c.0", 34, "dsi-pipe2-reset",
			    GPIO_ACTIVE_HIGH),
		{ },
	},
};

static int mdfld_chip_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	if (pci_enable_msi(pdev))
		dev_warn(dev->dev, "Enabling MSI failed!\n");
	dev_priv->regmap = mdfld_regmap;

	/* Associate the GPIO lines with the DRM device */
	mdfld_dsi_pipe_gpio_table.dev_id = dev_name(dev->dev);
	gpiod_add_lookup_table(&mdfld_dsi_pipe_gpio_table);

	return mid_chip_setup(dev);
}

const struct psb_ops mdfld_chip_ops = {
	.name = "mdfld",
	.pipes = 3,
	.crtcs = 3,
	.lvds_mask = (1 << 1),
	.hdmi_mask = (1 << 1),
	.cursor_needs_phys = 0,
	.sgx_offset = MRST_SGX_OFFSET,

	.chip_setup = mdfld_chip_setup,
	.crtc_helper = &mdfld_helper_funcs,
	.crtc_funcs = &psb_intel_crtc_funcs,

	.output_init = mdfld_output_init,

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	.backlight_init = mdfld_backlight_init,
#endif

	.save_regs = mdfld_save_registers,
	.restore_regs = mdfld_restore_registers,
	.save_crtc = gma_crtc_save,
	.restore_crtc = gma_crtc_restore,
	.power_down = mdfld_power_down,
	.power_up = mdfld_power_up,
};
