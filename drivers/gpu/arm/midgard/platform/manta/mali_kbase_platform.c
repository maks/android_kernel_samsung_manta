/* drivers/gpu/midgard/platform/manta/mali_kbase_platform.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_platform.c
 * Platform-dependent init.
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_uku.h>
#include <mali_kbase_mem.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_mem_linux.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <mach/map.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <mach/regs-clock.h>
#include <mach/pmu.h>
#include <mach/regs-pmu.h>
#include <linux/delay.h>
#include <platform/manta/mali_kbase_platform.h>
#include <platform/manta/mali_kbase_dvfs.h>

#include <mali_kbase_gator.h>

#define MALI_T6XX_DEFAULT_CLOCK 533000000

static struct clk *clk_g3d = NULL;
static int clk_g3d_status = 0;

static int kbase_platform_power_clock_init(struct kbase_device *kbdev)
{
	struct device *dev = kbdev->dev;
	int timeout;
	struct exynos_context *platform;

	platform = (struct exynos_context *)kbdev->platform_context;
	if (NULL == platform)
		panic("oops");

	/* Turn on G3D power */
	__raw_writel(0x7, EXYNOS5_G3D_CONFIGURATION);

	/* Wait for G3D power stability for 1ms */
	timeout = 10;
	while ((__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) != 0x7) {
		if (timeout == 0) {
			/* need to call panic  */
			panic("failed to turn on g3d power\n");
			goto out;
		}
		timeout--;
		udelay(100);
	}
	/* Turn on G3D clock */
	clk_g3d = clk_get(dev, "g3d");
	if (IS_ERR(clk_g3d)) {
		clk_g3d = NULL;
		printk(KERN_ERR "%s, %s(): failed to clk_get [clk_g3d]\n", __FILE__, __func__);
	} else {
		clk_enable(clk_g3d);
		clk_g3d_status = 1;
	}

	platform->sclk_g3d = clk_get(dev, "aclk_400_g3d");
	if (IS_ERR(platform->sclk_g3d)) {
		printk(KERN_ERR "%s, %s(): failed to clk_get [sclk_g3d]\n", __FILE__, __func__);
		goto out;
	}

	clk_set_rate(platform->sclk_g3d, MALI_T6XX_DEFAULT_CLOCK);
	if (IS_ERR(platform->sclk_g3d)) {
		printk(KERN_ERR "%s, %s(): failed to clk_set_rate [sclk_g3d] = %d\n", __FILE__, __func__, MALI_T6XX_DEFAULT_CLOCK);
		goto out;
	}
	(void)clk_enable(platform->sclk_g3d);

	return 0;
 out:
	return -EPERM;
}

int kbase_platform_clock_on(struct kbase_device *kbdev)
{
	struct exynos_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct exynos_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (clk_g3d_status == 1)
		return 0;

	if (clk_g3d)
		(void)clk_enable(clk_g3d);
	else
		(void)clk_enable(platform->sclk_g3d);

	clk_g3d_status = 1;

	return 0;
}

int kbase_platform_clock_off(struct kbase_device *kbdev)
{
	struct exynos_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct exynos_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (clk_g3d_status == 0)
		return 0;

	if (clk_g3d)
		(void)clk_disable(clk_g3d);
	else
		(void)clk_disable(platform->sclk_g3d);

	clk_g3d_status = 0;

	return 0;
}

int kbase_platform_is_power_on(void)
{
	return ((__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) == 0x7) ? 1 : 0;
}

static int kbase_platform_power_on(struct kbase_device *kbdev)
{
	int timeout;

	/* Turn on G3D  */
	__raw_writel(0x7, EXYNOS5_G3D_CONFIGURATION);

	/* Wait for G3D power stability */
	timeout = 1000;

	while ((__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) != 0x7) {
		if (timeout == 0) {
			/* need to call panic  */
			panic("failed to turn on g3d via g3d_configuration\n");
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(10);
	}

	KBASE_TIMELINE_GPU_POWER(kbdev, 1);

	return 0;
}

static int kbase_platform_power_off(struct kbase_device *kbdev)
{
	int timeout;

	/* Turn off G3D  */
	__raw_writel(0x0, EXYNOS5_G3D_CONFIGURATION);

	/* Wait for G3D power stability */
	timeout = 1000;

	while (__raw_readl(EXYNOS5_G3D_STATUS) & 0x7) {
		if (timeout == 0) {
			/* need to call panic */
			panic("failed to turn off g3d via g3d_configuration\n");
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(10);
	}

	KBASE_TIMELINE_GPU_POWER(kbdev, 0);

	return 0;
}

int kbase_platform_cmu_pmu_control(struct kbase_device *kbdev, int control)
{
	unsigned long flags;
	struct exynos_context *platform;
	if (!kbdev)
		return -ENODEV;

	platform = (struct exynos_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	spin_lock_irqsave(&platform->cmu_pmu_lock, flags);

	/* off */
	if (control == 0) {
		if (platform->cmu_pmu_status == 0) {
			spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);
			return 0;
		}

		if (kbase_platform_power_off(kbdev))
			panic("failed to turn off g3d power\n");
		if (kbase_platform_clock_off(kbdev))

			panic("failed to turn off sclk_g3d\n");

		platform->cmu_pmu_status = 0;
#ifdef MALI_RTPM_DEBUG
		printk(KERN_ERR "3D cmu_pmu_control - off\n");
#endif				/* MALI_RTPM_DEBUG */
	} else {
		/* on */
		if (platform->cmu_pmu_status == 1) {
			spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);
			return 0;
		}

		if (kbase_platform_clock_on(kbdev))
			panic("failed to turn on sclk_g3d\n");
		if (kbase_platform_power_on(kbdev))
			panic("failed to turn on g3d power\n");

		platform->cmu_pmu_status = 1;
#ifdef MALI_RTPM_DEBUG
		printk(KERN_ERR "3D cmu_pmu_control - on\n");
#endif				/* MALI_RTPM_DEBUG */
	}

	spin_unlock_irqrestore(&platform->cmu_pmu_lock, flags);

	return 0;
}

#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
static ssize_t show_clock(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	struct exynos_context *platform;
	ssize_t ret = 0;
	unsigned int clkrate;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	platform = (struct exynos_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (!platform->sclk_g3d)
		return -ENODEV;

	clkrate = clk_get_rate(platform->sclk_g3d);
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current sclk_g3d[G3D_BLK] = %dMhz", clkrate / 1000000);

	/* To be revised  */
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings : 720, 667, 612, 533, 450, 400, 350, 266, 160, 100Mhz");

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_clock(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	struct exynos_context *platform;
	unsigned int tmp = 0, freq = 0;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	platform = (struct exynos_context *)kbdev->platform_context;
	if (!platform)
		return -ENODEV;

	if (!platform->sclk_g3d)
		return -ENODEV;

	if (sysfs_streq("720", buf)) {
		freq = 720;
	} else if (sysfs_streq("667", buf)) {
		freq = 667;
	} else if (sysfs_streq("612", buf)) {
		freq = 612;
	} else if (sysfs_streq("533", buf)) {
		freq = 533;
	} else if (sysfs_streq("450", buf)) {
		freq = 450;
	} else if (sysfs_streq("400", buf)) {
		freq = 400;
	} else if (sysfs_streq("350", buf)) {
		freq = 350;
	} else if (sysfs_streq("266", buf)) {
		freq = 266;
	} else if (sysfs_streq("160", buf)) {
		freq = 160;
	} else if (sysfs_streq("100", buf)) {
		freq = 100;
	} else {
		dev_err(dev, "set_clock: invalid value\n");
		return -ENOENT;
	}

	kbase_platform_dvfs_set_level(kbdev, kbase_platform_dvfs_get_level(freq));
	/* Waiting for clock is stable */
	do {
		tmp = __raw_readl(EXYNOS5_CLKDIV_STAT_TOP0);
	} while (tmp & 0x1000000);

	return count;
}

static ssize_t show_fbdev(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int i;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < num_registered_fb; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "fb[%d] xres=%d, yres=%d, addr=0x%lx\n", i, registered_fb[i]->var.xres, registered_fb[i]->var.yres, registered_fb[i]->fix.smem_start);

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

typedef enum {
	L1_I_tag_RAM = 0x00,
	L1_I_data_RAM = 0x01,
	L1_I_BTB_RAM = 0x02,
	L1_I_GHB_RAM = 0x03,
	L1_I_TLB_RAM = 0x04,
	L1_I_indirect_predictor_RAM = 0x05,
	L1_D_tag_RAM = 0x08,
	L1_D_data_RAM = 0x09,
	L1_D_load_TLB_array = 0x0A,
	L1_D_store_TLB_array = 0x0B,
	L2_tag_RAM = 0x10,
	L2_data_RAM = 0x11,
	L2_snoop_tag_RAM = 0x12,
	L2_data_ECC_RAM = 0x13,
	L2_dirty_RAM = 0x14,
	L2_TLB_RAM = 0x18
} RAMID_type;

static inline void asm_ramindex_mrc(u32 *DL1Data0, u32 *DL1Data1, u32 *DL1Data2, u32 *DL1Data3)
{
	u32 val;

	if (DL1Data0) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 0" : "=r" (val));
		*DL1Data0 = val;
	}
	if (DL1Data1) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 1" : "=r" (val));
		*DL1Data1 = val;
	}
	if (DL1Data2) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 2" : "=r" (val));
		*DL1Data2 = val;
	}
	if (DL1Data3) {
		asm volatile ("mrc p15, 0, %0, c15, c1, 3" : "=r" (val));
		*DL1Data3 = val;
	}
}

static inline void asm_ramindex_mcr(u32 val)
{
	asm volatile ("mcr p15, 0, %0, c15, c4, 0" : : "r" (val));
	asm volatile ("dsb");
	asm volatile ("isb");
}

static void get_tlb_array(u32 val, u32 *DL1Data0, u32 *DL1Data1, u32 *DL1Data2, u32 *DL1Data3)
{
	asm_ramindex_mcr(val);
	asm_ramindex_mrc(DL1Data0, DL1Data1, DL1Data2, DL1Data3);
}

static RAMID_type ramindex = L1_D_load_TLB_array;
static ssize_t show_dtlb(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int entries, ways;
	u32 DL1Data0 = 0, DL1Data1 = 0, DL1Data2 = 0, DL1Data3 = 0;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	/* L1-I tag RAM */
	if (ramindex == L1_I_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I data RAM */
	else if (ramindex == L1_I_data_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I BTB RAM */
	else if (ramindex == L1_I_BTB_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I GHB RAM */
	else if (ramindex == L1_I_GHB_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-I TLB RAM */
	else if (ramindex == L1_I_TLB_RAM) {
		printk(KERN_DEBUG "L1-I TLB RAM\n");
		for (entries = 0; entries < 32; entries++) {
			get_tlb_array((((u8) ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, NULL);
			printk(KERN_DEBUG "entries[%d], DL1Data0=%08x, DL1Data1=%08x DL1Data2=%08x\n", entries, DL1Data0, DL1Data1 & 0xffff, 0x0);
		}
	}
	/* L1-I indirect predictor RAM */
	else if (ramindex == L1_I_indirect_predictor_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-D tag RAM */
	else if (ramindex == L1_D_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-D data RAM */
	else if (ramindex == L1_D_data_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L1-D load TLB array */
	else if (ramindex == L1_D_load_TLB_array) {
		printk(KERN_DEBUG "L1-D load TLB array\n");
		for (entries = 0; entries < 32; entries++) {
			get_tlb_array((((u8) ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
			printk(KERN_DEBUG "entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3 & 0x3f);
		}
	}
	/* L1-D store TLB array */
	else if (ramindex == L1_D_store_TLB_array) {
		printk(KERN_DEBUG "\nL1-D store TLB array\n");
		for (entries = 0; entries < 32; entries++) {
			get_tlb_array((((u8) ramindex) << 24) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
			printk(KERN_DEBUG "entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3 & 0x3f);
		}
	}
	/* L2 tag RAM */
	else if (ramindex == L2_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 data RAM */
	else if (ramindex == L2_data_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 snoop tag RAM */
	else if (ramindex == L2_snoop_tag_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 data ECC RAM */
	else if (ramindex == L2_data_ECC_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");
	/* L2 dirty RAM */
	else if (ramindex == L2_dirty_RAM)
		printk(KERN_DEBUG "Not implemented yet\n");

	/* L2 TLB array */
	else if (ramindex == L2_TLB_RAM) {
		printk(KERN_DEBUG "\nL2 TLB array\n");
		for (ways = 0; ways < 4; ways++) {
			for (entries = 0; entries < 512; entries++) {
				get_tlb_array((ramindex << 24) + (ways << 18) + entries, &DL1Data0, &DL1Data1, &DL1Data2, &DL1Data3);
				printk(KERN_DEBUG "ways[%d]:entries[%d], DL1Data0=%08x, DL1Data1=%08x, DL1Data2=%08x, DL1Data3=%08x\n", ways, entries, DL1Data0, DL1Data1, DL1Data2, DL1Data3);
			}
		}
	} else {
	}

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Succeeded...\n");

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}
	return ret;
}

static ssize_t set_dtlb(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if (sysfs_streq("L1_I_tag_RAM", buf)) {
		ramindex = L1_I_tag_RAM;
	} else if (sysfs_streq("L1_I_data_RAM", buf)) {
		ramindex = L1_I_data_RAM;
	} else if (sysfs_streq("L1_I_BTB_RAM", buf)) {
		ramindex = L1_I_BTB_RAM;
	} else if (sysfs_streq("L1_I_GHB_RAM", buf)) {
		ramindex = L1_I_GHB_RAM;
	} else if (sysfs_streq("L1_I_TLB_RAM", buf)) {
		ramindex = L1_I_TLB_RAM;
	} else if (sysfs_streq("L1_I_indirect_predictor_RAM", buf)) {
		ramindex = L1_I_indirect_predictor_RAM;
	} else if (sysfs_streq("L1_D_tag_RAM", buf)) {
		ramindex = L1_D_tag_RAM;
	} else if (sysfs_streq("L1_D_data_RAM", buf)) {
		ramindex = L1_D_data_RAM;
	} else if (sysfs_streq("L1_D_load_TLB_array", buf)) {
		ramindex = L1_D_load_TLB_array;
	} else if (sysfs_streq("L1_D_store_TLB_array", buf)) {
		ramindex = L1_D_store_TLB_array;
	} else if (sysfs_streq("L2_tag_RAM", buf)) {
		ramindex = L2_tag_RAM;
	} else if (sysfs_streq("L2_data_RAM", buf)) {
		ramindex = L2_data_RAM;
	} else if (sysfs_streq("L2_snoop_tag_RAM", buf)) {
		ramindex = L2_snoop_tag_RAM;
	} else if (sysfs_streq("L2_data_ECC_RAM", buf)) {
		ramindex = L2_data_ECC_RAM;
	} else if (sysfs_streq("L2_dirty_RAM", buf)) {
		ramindex = L2_dirty_RAM;
	} else if (sysfs_streq("L2_TLB_RAM", buf)) {
		ramindex = L2_TLB_RAM;
	} else {
		printk(KERN_DEBUG "Invalid value....\n\n");
		printk(KERN_DEBUG "Available options are one of below\n");
		printk(KERN_DEBUG "L1_I_tag_RAM, L1_I_data_RAM, L1_I_BTB_RAM\n");
		printk(KERN_DEBUG "L1_I_GHB_RAM, L1_I_TLB_RAM, L1_I_indirect_predictor_RAM\n");
		printk(KERN_DEBUG "L1_D_tag_RAM, L1_D_data_RAM, L1_D_load_TLB_array, L1_D_store_TLB_array\n");
		printk(KERN_DEBUG "L2_tag_RAM, L2_data_RAM, L2_snoop_tag_RAM, L2_data_ECC_RAM\n");
		printk(KERN_DEBUG "L2_dirty_RAM, L2_TLB_RAM\n");
	}

	return count;
}

static ssize_t show_vol(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int vol;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	kbase_platform_get_voltage(dev, &vol);
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current operating voltage for mali t6xx = %d, 0x%x", vol, __raw_readl(EXYNOS5_G3D_STATUS));

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static int get_clkout_cmu_top(int *val)
{
	*val = __raw_readl(EXYNOS5_CLKOUT_CMU_TOP);
	if ((*val & 0x1f) == 0xB)	/* CLKOUT is ACLK_400 in CLKOUT_CMU_TOP */
		return 1;
	else
		return 0;
}

static void set_clkout_for_3d(void)
{
#ifdef PMU_XCLKOUT_SET
	int tmp;

	tmp = 0x0;
	tmp |= 0x1000B;		/* ACLK_400 selected */
	tmp |= 9 << 8;		/* divided by (9 + 1) */
	__raw_writel(tmp, EXYNOS5_CLKOUT_CMU_TOP);

	tmp = 0x0;
	tmp |= 7 << 8;		/* CLKOUT_CMU_TOP selected */
	__raw_writel(tmp, S5P_PMU_DEBUG);
#endif
}

static ssize_t show_clkout(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int val;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if (get_clkout_cmu_top(&val))
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current CLKOUT is g3d divided by 10, CLKOUT_CMU_TOP=0x%x", val);
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current CLKOUT is not g3d, CLKOUT_CMU_TOP=0x%x", val);

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_clkout(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	if (sysfs_streq("3d", buf))
		set_clkout_for_3d();
	else
		printk(KERN_DEBUG "invalid val (only 3d is accepted\n");

	return count;
}

static ssize_t show_dvfs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (kbase_platform_dvfs_get_enable_status())
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "G3D DVFS is on\nutilisation:%d", kbase_platform_dvfs_get_utilisation());
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "G3D DVFS is off");
#else
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "G3D DVFS is disabled");
#endif

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_dvfs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
#ifdef CONFIG_MALI_MIDGARD_DVFS
	struct exynos_context *platform;
#endif

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	platform = (struct exynos_context *)kbdev->platform_context;
	if (sysfs_streq("off", buf)) {
		kbase_platform_dvfs_enable(false, MALI_DVFS_BL_CONFIG_FREQ);
		platform->dvfs_enabled = false;
	} else if (sysfs_streq("on", buf)) {
		kbase_platform_dvfs_enable(true, MALI_DVFS_START_FREQ);
		platform->dvfs_enabled = true;
	} else {
		printk(KERN_DEBUG "invalid val -only [on, off] is accepted\n");
	}
#else
	printk(KERN_DEBUG "G3D DVFS is disabled\n");
#endif
	return count;
}

static ssize_t show_upper_lock_dvfs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	int locked_level = -1;
#endif

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	locked_level = mali_get_dvfs_upper_locked_freq();
	if (locked_level > 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current Upper Lock Level = %dMhz", locked_level);
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the Upper Lock Level");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings : 667, 612, 533, 450, 400, 266, 160, 100, If you want to unlock : 720 or off");

#else
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "G3D DVFS is disabled. You can not set");
#endif

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_upper_lock_dvfs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (sysfs_streq("off", buf)) {
		mali_dvfs_freq_unlock();
	} else if (sysfs_streq("720", buf)) {
		mali_dvfs_freq_unlock();
	} else if (sysfs_streq("667", buf)) {
		mali_dvfs_freq_lock(8);
	} else if (sysfs_streq("612", buf)) {
		mali_dvfs_freq_lock(7);
	} else if (sysfs_streq("533", buf)) {
		mali_dvfs_freq_lock(6);
	} else if (sysfs_streq("450", buf)) {
		mali_dvfs_freq_lock(5);
	} else if (sysfs_streq("400", buf)) {
		mali_dvfs_freq_lock(4);
	} else if (sysfs_streq("350", buf)) {
		mali_dvfs_freq_lock(3);
	} else if (sysfs_streq("266", buf)) {
		mali_dvfs_freq_lock(2);
	} else if (sysfs_streq("160", buf)) {
		mali_dvfs_freq_lock(1);
	} else if (sysfs_streq("100", buf)) {
		mali_dvfs_freq_lock(0);
	} else {
		dev_err(dev, "set_clock: invalid value\n");
		dev_err(dev, "Possible settings : 667, 612, 533, 450, 400, 266, 160, 100, If you want to unlock : 720\n");
		return -ENOENT;
	}
#else				/* CONFIG_MALI_MIDGARD_DVFS */
	printk(KERN_DEBUG "G3D DVFS is disabled. You can not set\n");
#endif

	return count;
}

static ssize_t show_under_lock_dvfs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	int locked_level = -1;
#endif

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	locked_level = mali_get_dvfs_under_locked_freq();
	if (locked_level > 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current Under Lock Level = %dMhz", locked_level);
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the Under Lock Level");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings : 720, 667, 612, 533, 450, 400, 266, 160, If you want to unlock : 100 or off");

#else
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "G3D DVFS is disabled. You can not set");
#endif

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_under_lock_dvfs(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (sysfs_streq("off", buf)) {
		mali_dvfs_freq_under_unlock();
	} else if (sysfs_streq("720", buf)) {
		mali_dvfs_freq_under_lock(9);
	} else if (sysfs_streq("667", buf)) {
		mali_dvfs_freq_under_lock(8);
	} else if (sysfs_streq("612", buf)) {
		mali_dvfs_freq_under_lock(7);
	} else if (sysfs_streq("533", buf)) {
		mali_dvfs_freq_under_lock(6);
	} else if (sysfs_streq("450", buf)) {
		mali_dvfs_freq_under_lock(5);
	} else if (sysfs_streq("400", buf)) {
		mali_dvfs_freq_under_lock(4);
	} else if (sysfs_streq("350", buf)) {
		mali_dvfs_freq_under_lock(3);
	} else if (sysfs_streq("266", buf)) {
		mali_dvfs_freq_under_lock(2);
	} else if (sysfs_streq("160", buf)) {
		mali_dvfs_freq_under_lock(1);
	} else if (sysfs_streq("100", buf)) {
		mali_dvfs_freq_under_unlock();
	} else {
		dev_err(dev, "set_clock: invalid value\n");
		dev_err(dev, "Possible settings : 720, 667, 612, 533, 450, 400, 266, 160, If you want to unlock : 100 or off\n");
		return -ENOENT;
	}
#else
	printk(KERN_DEBUG "G3D DVFS is disabled. You can not set\n");
#endif /* CONFIG_MALI_MIDGARD_DVFS */

	return count;
}

static ssize_t show_asv(struct device *dev, struct device_attribute *attr, char *buf)
{

	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	ret = kbase_platform_dvfs_sprint_avs_table(buf);

	return ret;
}

static ssize_t set_asv(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	

	kbase_platform_dvfs_set_avs_table(buf);
  return count;
}

static ssize_t show_boost_time_duration(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int val;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	ret = snprintf(buf, PAGE_SIZE - ret, "Current gpu boost duration is %dmsecs\n", kbase_platform_dvfs_get_boost_time_duration() / USEC_PER_MSEC);

	return ret;
}

static ssize_t set_boost_time_duration(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	unsigned int duration = 0;
	int ret;

	ret = kstrtouint(buf, 10, &duration);
	if (ret == -EINVAL)
		return ret;
	if (ret == -ERANGE)
		return ret;

	if (duration < 3000)
		kbase_platform_dvfs_set_boost_time_duration(duration * USEC_PER_MSEC);

	return count;
}

static ssize_t show_gpu_boost_freq(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int val;

	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	ret = snprintf(buf, PAGE_SIZE - ret, "Current gpu boost freq is %d Mhz\n", kbase_platform_dvfs_get_gpu_boost_freq());

	return ret;
}

static ssize_t set_gpu_boost_freq(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	kbdev = dev_get_drvdata(dev);

	if (!kbdev)
		return -ENODEV;

	int ret;
  unsigned int freq;

	ret = kstrtouint(buf, 10, &freq);
	if (ret == -EINVAL)
		return ret;
	if (ret == -ERANGE)
		return ret;

	if (freq == 100 || freq == 160 || freq == 266 || freq == 350 || freq == 400 || freq == 450 || freq == 533)
		kbase_platform_dvfs_set_gpu_boost_freq(freq);
	else {
		dev_err(dev, "set_boost_clock: invalid value\n");
		dev_err(dev, "Possible settings : 533, 450, 400, 266, 160, 100\n");
		return -ENOENT;
	}

	return count;
}


/** The sysfs file @c clock, fbdev.
 *
 * This is used for obtaining information about the mali t6xx operating clock & framebuffer address,
 */
DEVICE_ATTR(clock, S_IRUGO | S_IWUSR, show_clock, set_clock);
DEVICE_ATTR(fbdev, S_IRUGO, show_fbdev, NULL);
DEVICE_ATTR(dtlb, S_IRUGO | S_IWUSR, show_dtlb, set_dtlb);
DEVICE_ATTR(vol, S_IRUGO | S_IWUSR, show_vol, NULL);
DEVICE_ATTR(clkout, S_IRUGO | S_IWUSR, show_clkout, set_clkout);
DEVICE_ATTR(dvfs, S_IRUGO | S_IWUSR, show_dvfs, set_dvfs);
DEVICE_ATTR(dvfs_upper_lock, S_IRUGO | S_IWUSR, show_upper_lock_dvfs, set_upper_lock_dvfs);
DEVICE_ATTR(dvfs_under_lock, S_IRUGO | S_IWUSR, show_under_lock_dvfs, set_under_lock_dvfs);
DEVICE_ATTR(asv, S_IRUGO | S_IWUSR, show_asv, set_asv);
DEVICE_ATTR(time_in_state, S_IRUGO | S_IWUSR, show_time_in_state, set_time_in_state);
DEVICE_ATTR(dvfs_boost_time_duration, S_IRUGO | S_IWUSR, show_boost_time_duration, set_boost_time_duration);
DEVICE_ATTR(dvfs_gpu_boost_freq, S_IRUGO | S_IWUSR, show_gpu_boost_freq, set_gpu_boost_freq);

int kbase_platform_create_sysfs_file(struct device *dev)
{
	if (device_create_file(dev, &dev_attr_clock)) {
		dev_err(dev, "Couldn't create sysfs file [clock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_fbdev)) {
		dev_err(dev, "Couldn't create sysfs file [fbdev]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dtlb)) {
		dev_err(dev, "Couldn't create sysfs file [dtlb]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_vol)) {
		dev_err(dev, "Couldn't create sysfs file [vol]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_clkout)) {
		dev_err(dev, "Couldn't create sysfs file [clkout]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs_upper_lock)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_upper_lock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs_under_lock)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_under_lock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_asv)) {
		dev_err(dev, "Couldn't create sysfs file [asv]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_time_in_state)) {
		dev_err(dev, "Couldn't create sysfs file [time_in_state]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs_boost_time_duration)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_boost_time_duration]\n");
		goto out;
	}
	if (device_create_file(dev, &dev_attr_dvfs_gpu_boost_freq)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_gpu_boost_freq]\n");
		goto out;
	}
	return 0;
 out:
	return -ENOENT;
}

void kbase_platform_remove_sysfs_file(struct device *dev)
{
	device_remove_file(dev, &dev_attr_clock);
	device_remove_file(dev, &dev_attr_fbdev);
	device_remove_file(dev, &dev_attr_dtlb);
	device_remove_file(dev, &dev_attr_vol);
	device_remove_file(dev, &dev_attr_clkout);
	device_remove_file(dev, &dev_attr_dvfs);
	device_remove_file(dev, &dev_attr_dvfs_upper_lock);
	device_remove_file(dev, &dev_attr_dvfs_under_lock);
	device_remove_file(dev, &dev_attr_asv);
	device_remove_file(dev, &dev_attr_time_in_state);
	device_remove_file(dev, &dev_attr_dvfs_boost_time_duration);
	device_remove_file(dev, &dev_attr_dvfs_gpu_boost_freq);
}
#endif				/* CONFIG_MALI_MIDGARD_DEBUG_SYS */

mali_error kbase_platform_init(struct kbase_device *kbdev)
{
	struct exynos_context *platform;

	platform = kmalloc(sizeof(struct exynos_context), GFP_KERNEL);

	if (NULL == platform)
		return MALI_ERROR_OUT_OF_MEMORY;

	kbdev->platform_context = (void *)platform;

	platform->cmu_pmu_status = 0;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	platform->utilisation = 0;
	platform->time_busy = 0;
	platform->time_idle = 0;
	platform->time_tick = 0;
	platform->dvfs_enabled = true;
#endif

	spin_lock_init(&platform->cmu_pmu_lock);

	if (kbase_platform_power_clock_init(kbdev))
		goto clock_init_fail;
#ifdef CONFIG_REGULATOR
	if (kbase_platform_regulator_init())
		goto regulator_init_fail;
#endif				/* CONFIG_REGULATOR */

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_platform_dvfs_init(kbdev);
#endif				/* CONFIG_MALI_MIDGARD_DVFS */
	kbase_platform_gpu_busy_init();

	/* Enable power */
	kbase_platform_cmu_pmu_control(kbdev, 1);
	return MALI_ERROR_NONE;

#ifdef CONFIG_REGULATOR
	kbase_platform_regulator_disable();
#endif				/* CONFIG_REGULATOR */
 regulator_init_fail:
 clock_init_fail:
	kfree(platform);

	return MALI_ERROR_FUNCTION_FAILED;
}

void kbase_platform_term(struct kbase_device *kbdev)
{
	struct exynos_context *platform;

	platform = (struct exynos_context *)kbdev->platform_context;

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_platform_dvfs_term();
#endif				/* CONFIG_MALI_MIDGARD_DVFS */

	/* Disable power */
	kbase_platform_cmu_pmu_control(kbdev, 0);
#ifdef CONFIG_REGULATOR
	kbase_platform_regulator_disable();
#endif				/* CONFIG_REGULATOR */
	kfree(kbdev->platform_context);
	kbdev->platform_context = 0;
	return;
}
