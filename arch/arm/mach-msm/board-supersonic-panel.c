/* linux/arch/arm/mach-msm/board-supersonic-panel.c
 *
 * Copyright (C) 2008 HTC Corporation.
 * Author: Jay Tu <jay_tu@htc.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>

#include <asm/io.h>
#include <asm/mach-types.h>
#include <mach/msm_fb.h>
#include <mach/msm_iomap.h>
#include <mach/vreg.h>
#include <mach/pmic.h>

#include "board-supersonic.h"
#include "devices.h"
#include "proc_comm.h"

#if 1
#define B(s...) printk(s)
#else
#define B(s...) do {} while(0)
#endif

extern int panel_type;

#define SPI_CONFIG              (0x00000000)
#define SPI_IO_CONTROL          (0x00000004)
#define SPI_OPERATIONAL         (0x00000030)
#define SPI_ERROR_FLAGS_EN      (0x00000038)
#define SPI_ERROR_FLAGS         (0x00000034)
#define SPI_OUTPUT_FIFO         (0x00000100)

extern int qspi_send_9bit(unsigned char id, unsigned data);

static struct cabc_t {
	struct led_classdev lcd_backlight;
	struct msm_mddi_client_data *client_data;
	struct mutex lock;
	unsigned long status;
} cabc;

struct reg_val_pair {
	unsigned reg;
	unsigned val;
};

struct lcm_cmd {
	unsigned char reg;
	unsigned char val;
	unsigned delay;
};

#define REG_WAIT (0xffff)

struct reg_val_pair epson_brightness_regs[] = {
	{0x1c, 0x10},
	{0x14a0, 0x01},
	{0x14a4, 0x110},
	{0x14b0, 0x3030},
	{0x14a8, 0x9c4},
	{0x14ac, 0xff0},
	{0x01, 0x00},
};

struct lcm_cmd epson_init_cmds[] = {
	{0x00, 0x11, 100},
	{0x00, 0xb9, 0},
	{0x01, 0xff, 0},
	{0x01, 0x83, 0},
	{0x01, 0x63, 0},
	{0x00, 0x3a, 0},
	{0x01, 0x50, 0},
};

struct lcm_cmd epson_uninit_cmds[] = {
	{0x00, 0x28, 0},
	{0x00, 0x10, 100},
};

enum {
	GATE_ON = 1 << 0,
};

static void suc_set_brightness(struct led_classdev *led_cdev,
				enum led_brightness val)
{
	int i;
	struct msm_mddi_client_data *client = cabc.client_data;
	unsigned int shrink_br = val;

	printk(KERN_DEBUG "%s: %d\n", __func__, val);
	if (test_bit(GATE_ON, &cabc.status) == 0)
		return;

	if (val < 30)
		shrink_br = 5;
	else if ((val >= 30) && (val <= 143))
		shrink_br = 104 * (val - 30) / 113 + 5;
	else
		shrink_br = 145 * (val - 144) / 111 + 110;

	mutex_lock(&cabc.lock);
	if (panel_type == 0)
	{
		// Epson
		int reg, val;
		for(i=0; i < ARRAY_SIZE(epson_brightness_regs); i++) {
			reg = cpu_to_le32(epson_brightness_regs[i].reg);
			val = cpu_to_le32(epson_brightness_regs[i].val);
			if (reg == REG_WAIT) {
				mdelay(epson_brightness_regs[0].val);
			}
			else {
				client->remote_write(client, val, reg);
			}
		}
		client->remote_write(client, shrink_br, 0x14b4);
	}
	else
	{
	  // nov
		client->remote_write(client, 0x00, 0x5500);
		client->remote_write(client, shrink_br, 0x5100);
	}
	mutex_unlock(&cabc.lock);
}

static enum led_brightness
suc_get_brightness(struct led_classdev *led_cdev)
{
	struct msm_mddi_client_data *client = cabc.client_data;

	printk(KERN_DEBUG "%s\n", __func__);

	if (panel_type == 0) {
		return client->remote_read(client, 0x14b4);
	} else {
		return client->remote_read(client, 0x5100);
	}
}

#define DEFAULT_BRIGHTNESS 100
static void suc_backlight_switch(int on)
{
	enum led_brightness val;

	printk(KERN_DEBUG "%s: %d\n", __func__, on);

	if (on) {
		printk(KERN_DEBUG "turn on backlight\n");
		set_bit(GATE_ON, &cabc.status);
		val = cabc.lcd_backlight.brightness;

		/* LED core uses get_brightness for default value
		 * If the physical layer is not ready, we should
		 * not count on it */
		if (val == 0)
			val = DEFAULT_BRIGHTNESS;
		suc_set_brightness(&cabc.lcd_backlight, val);
	} else {
		clear_bit(GATE_ON, &cabc.status);
		suc_set_brightness(&cabc.lcd_backlight, 0);
	}
}

static int suc_backlight_probe(struct platform_device *pdev)
{
	int err = -EIO;

	printk(KERN_DEBUG "%s\n", __func__);

	mutex_init(&cabc.lock);
	cabc.client_data = pdev->dev.platform_data;
	cabc.lcd_backlight.name = "lcd-backlight";
	cabc.lcd_backlight.brightness_set = suc_set_brightness;
	cabc.lcd_backlight.brightness_get = suc_get_brightness;
	err = led_classdev_register(&pdev->dev, &cabc.lcd_backlight);
	if (err)
		goto err_register_lcd_bl;
	return 0;

err_register_lcd_bl:
	led_classdev_unregister(&cabc.lcd_backlight);
	return err;
}

/* ------------------------------------------------------------------- */

static struct resource resources_msm_fb[] = {
	{
		.start = MSM_FB_BASE,
		.end = MSM_FB_BASE + MSM_FB_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct vreg *vreg_lcd_2v8;
static struct vreg *vreg_lcd_1v8;

struct reg_val_pair nov_init_seq[] = {
	{0xc000, 0x86},
	{0xc001, 0x00},
	{0xc002, 0x86},
	{0xc003, 0x00},
	{0xc100, 0x40},
	{0xc200, 0x02},
	{0xc202, 0x32},
	{0xe000, 0x0e},
	{0xe001, 0x2a},
	{0xe002, 0x33},
	{0xe003, 0x38},
	{0xe004, 0x1e},
	{0xe005, 0x30},
	{0xe006, 0x64},
	{0xe007, 0x3f},
	{0xe008, 0x21},
	{0xe009, 0x27},
	{0xe00a, 0x88},
	{0xe00b, 0x14},
	{0xe00c, 0x35},
	{0xe00d, 0x56},
	{0xe00e, 0x79},
	{0xe00f, 0x88},
	{0xe010, 0x55},
	{0xe011, 0x57},
	{0xe100, 0x0e},
	{0xe101, 0x2a},
	{0xe102, 0x33},
	{0xe103, 0x3b},
	{0xe104, 0x1e},
	{0xe105, 0x30},
	{0xe106, 0x64},
	{0xe107, 0x3f},
	{0xe108, 0x21},
	{0xe109, 0x27},
	{0xe10a, 0x88},
	{0xe10b, 0x14},
	{0xe10c, 0x35},
	{0xe10d, 0x56},
	{0xe10e, 0x79},
	{0xe10f, 0x88},
	{0xe110, 0x55},
	{0xe111, 0x57},

	{0xe200, 0x0E},
	{0xe201, 0x2A},
	{0xe202, 0x33},
	{0xe203, 0x3B},
	{0xe204, 0x1e},
	{0xe205, 0x30},
	{0xe206, 0x64},
	{0xe207, 0x3F},
	{0xe208, 0x21},
	{0xe209, 0x27},
	{0xe20A, 0x88},
	{0xe20B, 0x14},
	{0xe20C, 0x35},
	{0xe20D, 0x56},
	{0xe20E, 0x79},
	{0xe20F, 0xB8},
	{0xe210, 0x55},
	{0xe211, 0x57},

	{0xe300, 0x0E},
	{0xe301, 0x2A},
	{0xe302, 0x33},
	{0xe303, 0x3B},
	{0xe304, 0x1E},
	{0xe305, 0x30},
	{0xe306, 0x64},
	{0xe307, 0x3F},
	{0xe308, 0x21},
	{0xe309, 0x27},
	{0xe30A, 0x88},
	{0xe30B, 0x14},
	{0xe30C, 0x35},
	{0xe30D, 0x56},
	{0xe30E, 0x79},
	{0xe30F, 0xB8},
	{0xe310, 0x55},
	{0xe311, 0x57},
	{0xe400, 0x0E},
	{0xe401, 0x2A},
	{0xe402, 0x33},
	{0xe403, 0x3B},
	{0xe404, 0x1E},
	{0xe405, 0x30},
	{0xe406, 0x64},
	{0xe407, 0x3F},
	{0xe408, 0x21},
	{0xe409, 0x27},
	{0xe40A, 0x88},
	{0xe40B, 0x14},
	{0xe40C, 0x35},
	{0xe40D, 0x56},
	{0xe40E, 0x79},
	{0xe40F, 0xB8},
	{0xe410, 0x55},
	{0xe411, 0x57},
	{0xe500, 0x0E},
	{0xe501, 0x2A},
	{0xe502, 0x33},
	{0xe503, 0x3B},
	{0xe504, 0x1E},
	{0xe505, 0x30},
	{0xe506, 0x64},
	{0xe507, 0x3F},
	{0xe508, 0x21},
	{0xe509, 0x27},
	{0xe50A, 0x88},
	{0xe50B, 0x14},
	{0xe50C, 0x35},
	{0xe50D, 0x56},
	{0xe50E, 0x79},
	{0xe50F, 0xB8},
	{0xe510, 0x55},
	{0xe511, 0x57},

	{0x3a00, 0x05},

	/* cabc */
	{0x4e00, 0x00},
	{0x5e00, 0x00},
	{0x6a01, 0x00},
	{0x6a02, 0x03},
	{0x5100, 0xff},
	{0x5301, 0x10},
	{0x6A18, 0xff},
	{0x6A17, 0x01},
	{0xF402, 0x14},

	{0x3500, 0x00},
	{0x1100, 0x0},
	{REG_WAIT, 120},
};

struct reg_val_pair epson_init_seq[] = {
	{0x1C, 0x15D0},  	 //; 0
	{0x20, 0x3047},  	 //; 1
	{0x24, 0x401A},  	 //; 2
	{0x28, 0x31A},  	 //; 3
	{0x2C, 1},  	 //  ; 4
	{0xFFFF, 4},  	 //; 5
	{0x84, 0x215},  	 //; 6
	{0x88, 0x38},  	 //; 7
	{0x8C, 0x2113},  	 //; 8
	{0x2C, 2},  	 //  ; 9
	{0xFFFF, 4},  	 //; 10
	{0x2C, 3},  	 //  ; 11
	{0x100, 0x3702},  	 //; 12
	{0x104, 0x180},  	 //; 13
	{0x140, 0x3F},  	 //; 14
	{0x144, 0xEF},  	 //; 15
	{0x148, 0x16},  	 //; 16
	{0x14C, 5},  	 // ; 17
	{0x150, 6},  	 // ; 18
	{0x154, 0x32B},  	 //; 19
	{0x158, 0x31F},  	 //; 20
	{0x15C, 9},  	 // ; 21
	{0x160, 2},  	 // ; 22
	{0x164, 3},  	 // ; 23
	{0x168, 0xA2},  	 //; 24
	{0x180, 0x57},  	 //; 25
	{0x184, 0xDB},  	 //; 26
	{0x188, 0xE3},  	 //; 27
	{0x18C, 0},  	 // ; 28
	{0x190, 0},  	 // ; 29
	{0x280, 0},  	 // ; 30
	{0x284, 2},  	 // ; 31
	{0x288, 0},  	 // ; 32
	{0x28C, 1},  	 // ; 33
	{0x294, 0},  	 // ; 34
	{0x400, 0x8000},  	 //; 35
	{0x404, 0x1001},  	 //; 36
	{0x480, 1},  	 // ; 37
	{0x500, 0},  	 // ; 38
	{0x504, 0x11},  	 //; 39
	{0x508, 0},  	 // ; 40
	{0x510, 0},  	 // ; 41
	{0x518, 0x2E},  	 //; 42
	{0x51C, 0xC7},  	 //; 43
	{0x520, 0x1DF},  	 //; 44
	{0x524, 0x31F},  	 //; 45
	{0x528, 0},  	 // ; 46
	{0x52C, 0},  	 // ; 47
	{0x530, 0},  	 // ; 48
	{0x534, 0},  	 // ; 49
	{0x604, 0x108},  	 //; 50
	{0x60C, 0},  	 // ; 51
	{0x610, 0xFF},  	 //; 52
	{0x648, 0x20},  	 //; 53
	{0x800, 0},  	 // ; 54
	{0x804, 0xA},  	 //; 55
	{0x808, 0x400},  	 //; 56
	{0x80C, 0x400},  	 //; 57
	{0x814, 0},  	 // ; 58
	{0x81C, 0},  	 // ; 59
	{0x824, 0x2E},  	 //; 60
	{0x828, 0xC7},  	 //; 61
	{0x82C, 0x1DF},  	 //; 62
	{0x830, 0x31F},  	 //; 63
	{0x834, 0},  	 // ; 64
	{0x838, 0},  	 // ; 65
	{0x83C, 0},  	 // ; 66
	{0x840, 0},  	 // ; 67
	{0x844, 0x1DF},  	 //; 68
	{0x848, 0x31F},  	 //; 69
	{0x870, 0x64},  	 //; 70
	{0x874, 0x64},  	 //; 71
	{0x878, 0xC7},  	 //; 72
	{0x87C, 0xC7},  	 //; 73
	{0x1410, 4},  	 //; 74
	{0x1414, 0xFF},  	 //; 75
	{0x1420, 0},  	 //; 76
	{0x1424, 0},  	 //; 77
	{0x1428, 0x1DF},  	 //; 78
	{0x142C, 0x31F},  	 //; 79
	{0x1430, 0xDC00},  	 //; 80
	{0x1434, 5},  	 //; 81
	{0x1440, 0},  	 //; 82
	{0x1444, 0},  	 //; 83
	{0x1448, 0x1DF},  	 //; 84
	{0x144C, 0x31F},  	 //; 85
	{0x1450, 0},  	 //; 86
	{0x1454, 0},  	 //; 87
	{0x1458, 0x1DF},  	 //; 88
	{0x145C, 0x31F},  	 //; 89
	{0x1460, 0},  	 //; 90
	{0x1464, 0},  	 //; 91
	{0x1468, 0x1DF},  	 //; 92
	{0x146C, 0x31F},  	 //; 93
	{0x1470, 0},  	 //; 94
	{0x1474, 0},  	 //; 95
	{0x1478, 0x1DF},  	 //; 96
	{0x147C, 0x31F},  	 //; 97
	{0x14A4, 0x110},  	 //; 98
	{0x14A8, 0xAFC8},  	 //; 99
	{0x14AC, 0xFF0},  	 //; 100
	{0x14B0, 0x202},  	 //; 101
	{0x14B4, 0x80},  	 //; 102
	{0x14A0, 1},  	 //; 103
	{0x1508, 0},  	 //; 104
	{0x150C, 0},  	 //; 105
	{0x1510, 0},  	 //; 106
	{0x1514, 0},  	 //; 107
	{0x1520, 0},  	 //; 108
	{0x1524, 0},  	 //; 109
	{0x1528, 0},  	 //; 110
	{0x152C, 0},  	 //; 111
	{0x1530, 0},  	 //; 112
	{0x1534, 0},  	 //; 113
	{0x1538, 0},  	 //; 114
	{0x153C, 0},  	 //; 115
	{0x1540, 0},  	 //; 116
	{0x1544, 0},  	 //; 117
	{0x1548, 0},  	 //; 118
	{0x154C, 0},  	 //; 119
	{0x1550, 0},  	 //; 120
	{0x1554, 0},  	 //; 121
	{0x1558, 0},  	 //; 122
	{0x1600, 0},  	 //; 123
	{0x1604, 0x20},  	 //; 124
	{0x1608, 0x40},  	 //; 125
	{0x160C, 0x60},  	 //; 126
	{0x1610, 0x80},  	 //; 127
	{0x1614, 0xA0},  	 //; 128
	{0x1618, 0xC0},  	 //; 129
	{0x161C, 0xE0},  	 //; 130
	{0x1620, 0x100},  	 //; 131
	{0x1624, 0},  	 //; 132
	{0x1628, 0x20},  	 //; 133
	{0x162C, 0x40},  	 //; 134
	{0x1630, 0x60},  	 //; 135
	{0x1634, 0x80},  	 //; 136
	{0x1638, 0xA0},  	 //; 137
	{0x163C, 0xC0},  	 //; 138
	{0x1640, 0xE0},  	 //; 139
	{0x1644, 0x100},  	 //; 140
	{0x1648, 0},  	 //; 141
	{0x164C, 0x20},  	 //; 142
	{0x1650, 0x40},  	 //; 143
	{0x1654, 0x60},  	 //; 144
	{0x1658, 0x80},  	 //; 145
	{0x165C, 0xA0},  	 //; 146
	{0x1660, 0xC0},  	 //; 147
	{0x1664, 0xE0},  	 //; 148
	{0x1668, 0x100},  	 //; 149
	{0x1680, 0},  	 //; 150
	{0x1684, 0},  	 //; 151
	{0x1688, 0},  	 //; 152
	{0x168C, 0},  	 //; 153
	{0x1694, 0},  	 //; 154
	{0x16A0, 0},  	 //; 155
	{0x16A4, 0},  	 //; 156
	{0x16A8, 0},  	 //; 157
	{0x16AC, 0},  	 //; 158
	{0x16B4, 0},  	 //; 159
	{0x16C0, 0},  	 //; 160
	{0x16C4, 0},  	 //; 161
	{0x16C8, 0},  	 //; 162
	{0x16CC, 0},  	 //; 163
	{0x16D4, 0},  	 //; 164
	{0x16E0, 0},  	 //; 165
	{0x16E4, 0},  	 //; 166
	{0x16E8, 0},  	 //; 167
	{0x16EC, 0},  	 //; 168
	{0x16F4, 0},  	 //; 169
	{0x1700, 0},  	 //; 170
	{0x1704, 0},  	 //; 171
	{0x1708, 0},  	 //; 172
	{0x170C, 0},  	 //; 173
	{0x1714, 0},  	 //; 174
	{0x1720, 0},  	 //; 175
	{0x1724, 0},  	 //; 176
	{0x1728, 0},  	 //; 177
	{0x172C, 0},  	 //; 178
	{0x1734, 0},  	 //; 179
	{0x1740, 0},  	 //; 180
	{0x1744, 0},  	 //; 181
	{0x1748, 0},  	 //; 182
	{0x174C, 0},  	 //; 183
	{0x1754, 0},  	 //; 184
	{0x1760, 0},  	 //; 185
	{0x1764, 0},  	 //; 186
	{0x1768, 0},  	 //; 187
	{0x176C, 0},  	 //; 188
	{0x1774, 0},  	 //; 189
	{0x300, 0x7000},  	 //; 190
	{0x304, 0},  	 // ; 191
	{0x308, 0},  	 // ; 192
	{0x30C, 0},  	 // ; 193
	{0x310, 0},  	 // ; 194
	{0x314, 0},  	 // ; 195
	{0x318, 0xF7FF},  	 //; 196
	{0x31C, 0xFFFF},  	 //; 197
	{0x320, 0xF},  	 //; 198
	{0x324, 0x7000},  	 //; 199
	{0x328, 0},  	 // ; 200
	{0x32C, 0},  	 // ; 201

};

static int
supersonic_mddi_init(struct msm_mddi_bridge_platform_data *bridge_data,
		     struct msm_mddi_client_data *client_data)
{
	int i;
	unsigned reg, val, delay;
	int sz = ARRAY_SIZE(epson_init_cmds);
	struct lcm_cmd init_cmds[sz];

	printk(KERN_DEBUG "%s: paneltype = %d\n", __func__, panel_type);

	client_data->auto_hibernate(client_data, 0);

	if (panel_type == 0) {
		/* Epson panel */

		for (i = 0; i < ARRAY_SIZE(epson_init_seq); i++) {
			reg = cpu_to_le32(epson_init_seq[i].reg);
			val = cpu_to_le32(epson_init_seq[i].val);
			if (reg == REG_WAIT) {
				mdelay(val);
			} else {
				client_data->remote_write(client_data, val, reg);
			}
		}

		client_data->auto_hibernate(client_data, 1);

		memcpy(init_cmds, epson_init_cmds, sizeof(struct lcm_cmd) * sz);
		for (i = 0; i < sz; i++) {
			reg = init_cmds[i].reg;
			val = init_cmds[i].val;
			delay = init_cmds[i].delay;
			if (qspi_send_9bit(reg, val) < 0) {
				printk(KERN_ERR "%s: spi_write fail (%02x, %02x)!\n", __func__, reg, val);
			} else if (delay > 0) {
				msleep(delay);
			}
		}

	} else {
		/* Novatec panel (panel_type == 1) */

		for (i = 0; i < ARRAY_SIZE(nov_init_seq); i++) {
			reg = cpu_to_le32(nov_init_seq[i].reg);
			val = cpu_to_le32(nov_init_seq[i].val);
			if (reg == REG_WAIT)
				msleep(val);
			else
				client_data->remote_write(client_data, val, reg);
		}

		client_data->auto_hibernate(client_data, 1);
	}
	return 0;
}

static int
supersonic_mddi_uninit(struct msm_mddi_bridge_platform_data *bridge_data,
			struct msm_mddi_client_data *client_data)
{
	int i;
	unsigned char reg, val;
	unsigned delay;

	printk(KERN_DEBUG "%s\n", __func__);

	if (panel_type == 0) {
		// Epson
		for (i = 0; i < ARRAY_SIZE(epson_uninit_cmds); i++) {
			reg = epson_uninit_cmds[i].reg;
			val = epson_uninit_cmds[i].val;
			delay = epson_uninit_cmds[i].delay;
			if (qspi_send_9bit(reg, val) < 0) {
				printk(KERN_ERR "%s: spi_write fail (%02x, %02x)!\n", __func__, reg, val);
			}
			else if (delay) {
				mdelay(delay);
			}
		}

	} else {
		client_data->remote_write(client_data, 0, 0x2800);
	}
	return 0;
}

/* FIXME: remove after XA03 */
static int backlight_control(int on)
{
	struct i2c_adapter *adap = i2c_get_adapter(0);
	struct i2c_msg msg;
	u8 buf[] = {0x90, 0x00, 0x00, 0x08};
	int ret = -EIO, max_retry = 3;

	printk(KERN_DEBUG "%s: %d\n", __func__, on);

	msg.addr = 0xcc >> 1;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (on == 0)
		buf[0] = 0x91;

	while (max_retry--) {
		ret = i2c_transfer(adap, &msg, 1);
		if (ret != 1)
			msleep(1);
		else {
			ret = 0;
			break;
		}
		ret = -EIO;
	}

	if (ret)
		printk(KERN_ERR "backlight control fail\n");
	return 0;
}

static int
supersonic_panel_blank(struct msm_mddi_bridge_platform_data *bridge_data,
			struct msm_mddi_client_data *client_data)
{
	printk(KERN_DEBUG "%s\n", __func__);
	suc_backlight_switch(LED_OFF);
	backlight_control(0);
	return 0;
}

static int
supersonic_panel_unblank(struct msm_mddi_bridge_platform_data *bridge_data,
			struct msm_mddi_client_data *client_data)
{
	printk(KERN_DEBUG "%s\n", __func__);
	if (panel_type == 1) {
		// nov
		suc_backlight_switch(LED_FULL);
		client_data->remote_write(client_data, 0x00, 0x2900);
		msleep(100);
		client_data->remote_write(client_data, 0x24, 0x5300);
	} else {
		// epson
		suc_backlight_switch(LED_FULL);
		client_data->remote_write(client_data, 0x4000, 0x600);
		msleep(10);
		qspi_send_9bit(0x00, 0x29);
	}
	backlight_control(1);
	return 0;
}

static struct msm_mddi_bridge_platform_data novatec_client_data = {
	.init = supersonic_mddi_init,
	.uninit = supersonic_mddi_uninit,
	.blank = supersonic_panel_blank,
	.unblank = supersonic_panel_unblank,
	.fb_data = {
		.xres = 480,
		.yres = 800,
		.width = 48,
		.height = 80,
		.output_format = 0,
	},
	.panel_conf = {
		.caps = MSMFB_CAP_CABC,
	},
};

static struct msm_mddi_bridge_platform_data epson_client_data = {
	.init = supersonic_mddi_init,
	.uninit = supersonic_mddi_uninit,
	.blank = supersonic_panel_blank,
	.unblank = supersonic_panel_unblank,
	.fb_data = {
		.xres = 480,
		.yres = 800,
		.width = 48,
		.height = 80,
		.output_format = 0,
	},
	.panel_conf = {
		.caps = MSMFB_CAP_CABC,
	},
};

static void
mddi_novatec_power(struct msm_mddi_client_data *client_data, int on)
{
	unsigned id, on_off = 1;

	printk(KERN_DEBUG "%s: on=%d\n", __func__, on);
	B(KERN_DEBUG "%s: power %s.\n", __func__, on ? "on" : "off");

	if (on) {
		on_off = 1;
		/* 2V8 */
		id = PM_VREG_PDOWN_SYNT_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_enable(vreg_lcd_2v8);

		/* 1V8 */
		id = PM_VREG_PDOWN_AUX_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_enable(vreg_lcd_1v8);
		mdelay(15);

		gpio_set_value(SUPERSONIC_LCD_RST, 1);
		mdelay(1);
		gpio_set_value(SUPERSONIC_LCD_RST, 0);
		mdelay(5);
		gpio_set_value(SUPERSONIC_LCD_RST, 1);
		msleep(50);
	} else {
		on_off = 0;
		gpio_set_value(SUPERSONIC_LCD_RST, 0);
		mdelay(120);

		/* 1V8 */
		id = PM_VREG_PDOWN_AUX_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_disable(vreg_lcd_1v8);

		/* 2V8 */
		id = PM_VREG_PDOWN_SYNT_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_disable(vreg_lcd_2v8);
	}
}

static void
mddi_epson_power(struct msm_mddi_client_data *client_data, int on)
{
	unsigned id, on_off = 1;

	printk(KERN_DEBUG "%s: on=%d\n", __func__, on);

	if (on) {
		on_off = 1;
		gpio_set_value(SUPERSONIC_LCD_RST_XD, 1);

		/* 2V8 */
		id = PM_VREG_PDOWN_SYNT_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_enable(vreg_lcd_2v8);

		mdelay(5);
		gpio_set_value(SUPERSONIC_GPIO_ISET, 1);

		/* 1V8 */
		id = PM_VREG_PDOWN_AUX_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_enable(vreg_lcd_1v8);
		mdelay(10);

		gpio_set_value(SUPERSONIC_LCD_RST_FOO_XD, 1);
		mdelay(2);

		gpio_set_value(SUPERSONIC_LCD_RST, 1);
		mdelay(1);
		gpio_set_value(SUPERSONIC_LCD_RST, 0);
		mdelay(5);
		gpio_set_value(SUPERSONIC_LCD_RST, 1);
		msleep(50);

	} else {
		on_off = 0;
		gpio_set_value(SUPERSONIC_LCD_RST, 0);
		mdelay(2);

		gpio_set_value(SUPERSONIC_LCD_RST_FOO_XD, 0);
		mdelay(120);

		gpio_set_value(SUPERSONIC_GPIO_ISET, 0);

		/* 1V8 */
		id = PM_VREG_PDOWN_AUX_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_disable(vreg_lcd_1v8);

		mdelay(5);
		gpio_set_value(SUPERSONIC_LCD_RST_XD, 0);

		/* 2V8 */
		id = PM_VREG_PDOWN_SYNT_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);
		vreg_disable(vreg_lcd_2v8);
	}
}

static struct msm_mddi_platform_data mddi_pdata = {
	.clk_rate = 384000000,
	.fb_resource = resources_msm_fb,
	.num_clients = 2,
	.client_platform_data = {
		{
			.product_id = (0xb9f6 << 16 | 0x5582),
			.name = "mddi_c_b9f6_5582",
			.id = 1,
			.client_data = &novatec_client_data,
			.clk_rate = 0,
		},
		{
			.product_id = (0x4ca3 << 16 | 0x0000),
			.name = "mddi_c_4ca3_0000",
			.id = 0,
			.client_data = &epson_client_data,
			.clk_rate = 0,
		},
	},
};

static struct platform_driver suc_backlight_driver = {
	.probe = suc_backlight_probe,
	.driver = {
		.owner = THIS_MODULE,
	},
};

static struct msm_mdp_platform_data mdp_pdata = {
	.dma_channel = MDP_DMA_P;
};

int __init supersonic_init_panel(void)
{
	int rc;

	if (!machine_is_supersonic())
		return -1;

	B(KERN_INFO "%s: enter.\n", __func__);

	vreg_lcd_1v8 = vreg_get(0, "gp4");
	if (IS_ERR(vreg_lcd_1v8))
		return PTR_ERR(vreg_lcd_1v8);

	vreg_lcd_2v8 = vreg_get(0, "synt");
	if (IS_ERR(vreg_lcd_2v8))
		return PTR_ERR(vreg_lcd_2v8);

	// TODO: something is based on panel_type here -maejrep

	msm_device_mdp.dev.platform_data = &mdp_pdata;
	rc = platform_device_register(&msm_device_mdp);
	if (rc)
		return rc;

	if (panel_type == 0) {
		mddi_pdata.power_client = mddi_epson_power;
	} else {
		mddi_pdata.power_client = mddi_novatec_power;
	}

	msm_device_mddi0.dev.platform_data = &mddi_pdata;
	rc = platform_device_register(&msm_device_mddi0);
	if (rc)
		return rc;

	if (panel_type == 0) {
		suc_backlight_driver.driver.name = "eps_cabc";
	} else {
		suc_backlight_driver.driver.name = "nov_cabc";
	}
	rc = platform_driver_register(&suc_backlight_driver);
	if (rc)
		return rc;

	return 0;
}

device_initcall(supersonic_init_panel);
