/*
 * clevo_platform.c
 * 
 * Copyright (C) 2022-2023 Slimbook <dev@slimbook.es>
 * 
 * Based on clevo-xsm-wmi by Arnoud Willemsen
 * Copyright (C) 2014-2016 Arnoud Willemsen <mail@lynthium.com>
 *
 * Based on tuxedo-keyboard by TUXEDO Computers GmbH
 * Copyright (c) 2020-2021 TUXEDO Computers GmbH <tux@tuxedocomputers.com>
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the  GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty  of
 * MERCHANTABILITY or FITNESS FOR  A PARTICULAR  PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should  have received  a copy of  the GNU General  Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#define MODULE_NAME KBUILD_MODNAME

MODULE_AUTHOR("Slimbook");
MODULE_DESCRIPTION(MODULE_NAME);
MODULE_LICENSE("GPL");

#define CLEVO_ACPI_DSM_UUID "93F224E4-FBDC-4BBF-ADD6-DB71BDC0AFAD"

#define CLEVO_V1_EVENT_GUID "ABBC0F6B-8EA1-11D1-00A0-C90629100000"
#define CLEVO_V1_GET_GUID "ABBC0F6D-8EA1-11D1-00A0-C90629100000"

#define CLEVO_V2_EVENT_GUID "A6FEA33E-DABF-46F5-BFC8-460D961BEC9F"
#define CLEVO_V2_GET_GUID "2BC49DEF-7B15-4F05-8BB7-EE37B9547C0B"

#define WMI_SUBMETHOD_ID_GET_EVENT 0x01
#define WMI_SUBMETHOD_ID_GET_AP 0x46
#define WMI_SUBMETHOD_ID_SET_KB_LEDS 0x67
#define WMI_SUBMETHOD_ID_SET_KB_LEDS_BW 0x27
#define WMI_SUBMETHOD_ID_GET_BIOS_1 0x52
#define WMI_SUBMETHOD_ID_GET_BIOS_2 0x7A

#define EVENT_CODE_DECREASE_BACKLIGHT 0x81
#define EVENT_CODE_INCREASE_BACKLIGHT 0x82
#define EVENT_CODE_DECREASE_BACKLIGHT_2 0x20
#define EVENT_CODE_INCREASE_BACKLIGHT_2 0x21
#define EVENT_CODE_NEXT_BLINKING_PATTERN 0x83
#define EVENT_CODE_TOGGLE_STATE 0x9F
#define EVENT_CODE_TOGGLE_STATE_2 0x3F

#define REGION_LEFT 0xF0000000
#define REGION_CENTER 0xF1000000
#define REGION_RIGHT 0xF2000000
#define REGION_EXTRA 0xF3000000

#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 255
#define BRIGHTNESS_MAX_BW 5
#define BRIGHTNESS_DEFAULT BRIGHTNESS_MAX
#define BRIGHTNESS_DEFAULT_BW BRIGHTNESS_MAX_BW
#define BRIGHTNESS_STEP 25

#define KB_COLOR_DEFAULT 0xFFFFFF
#define DEFAULT_BLINKING_PATTERN 0

#define CLEVO_MODEL_UNKNOWN 0x00
#define CLEVO_MODEL_V1 0x01
#define CLEVO_MODEL_V2 0x02

#define KB_TYPE_BW 0
#define KB_TYPE_RGB 1

u32 model = CLEVO_MODEL_UNKNOWN;

struct acpi_device *active_device = NULL;

struct color_t
{
	u32 code;
	char *name;
};

struct color_list_t
{
	uint size;
	struct color_t colors[];
};

static struct color_list_t color_list = {
	.size = 8,
	.colors = {
		{.name = "BLACK", .code = 0x000000},   // 0
		{.name = "RED", .code = 0xFF0000},	   // 1
		{.name = "GREEN", .code = 0x00FF00},   // 2
		{.name = "BLUE", .code = 0x0000FF},	   // 3
		{.name = "YELLOW", .code = 0xFFFF00},  // 4
		{.name = "MAGENTA", .code = 0xFF00FF}, // 5
		{.name = "CYAN", .code = 0x00FFFF},	   // 6
		{.name = "WHITE", .code = 0xFFFFFF},   // 7
	}};

struct blinking_pattern_t {
	u8 key;
	u32 value;
	const char *const name;
};

static struct blinking_pattern_t blinking_patterns[] = {
	{ .key = 0,.value = 0,.name = "CUSTOM"},
	{ .key = 1,.value = 0x1002a000,.name = "BREATHE"},
	{ .key = 2,.value = 0x33010000,.name = "CYCLE"},
	{ .key = 3,.value = 0x80000000,.name = "DANCE"},
	{ .key = 4,.value = 0xA0000000,.name = "FLASH"},
	{ .key = 5,.value = 0x70000000,.name = "RANDOM_COLOR"},
	{ .key = 6,.value = 0x90000000,.name = "TEMPO"},
	{ .key = 7,.value = 0xB0000000,.name = "WAVE"}
};

// Keyboard struct
struct kbd_led_state_t
{
	u8 mode; /* 0 bw, 1 rgb */
	u8 has_extra;
	u8 enabled;

	struct
	{
		u32 left;
		u32 center;
		u32 right;
		u32 extra;
	} color;

	u8 brightness;
	u8 blinking_pattern;
	u8 whole_kbd_color;
};

static struct kbd_led_state_t kbd_led_state = {
	.mode = KB_TYPE_BW,
	.has_extra = 0,
	.enabled = 1,
	.color = {
		.left = KB_COLOR_DEFAULT, .center = KB_COLOR_DEFAULT, .right = KB_COLOR_DEFAULT, .extra = KB_COLOR_DEFAULT},
	.brightness = BRIGHTNESS_DEFAULT,
	.blinking_pattern = DEFAULT_BLINKING_PATTERN,
	.whole_kbd_color = 5

};


// forward declarations

static int blinking_pattern_id_validator(const char *value,
                                         const struct kernel_param *blinking_pattern_param);

static int brightness_validator(const char *val,
                                const struct kernel_param *brightness_param);

static void set_brightness(u8 brightness);

static void set_enabled(u8 state);

static int set_color(u32 region, u32 color);

static int set_color_string_region(const char *color_string, size_t size, u32 region)
{
	u32 colorcode;
	int err = kstrtouint(color_string, 16, &colorcode);

	if (err) {
		return err;
	}

	if (!set_color(region, colorcode)) {
		// after succesfully setting color, update our state struct
		// depending on which region was changed
		switch (region) {
		case REGION_LEFT:
			kbd_led_state.color.left = colorcode;
			break;
		case REGION_CENTER:
			kbd_led_state.color.center = colorcode;
			break;
		case REGION_RIGHT:
			kbd_led_state.color.right = colorcode;
			break;
		case REGION_EXTRA:
			kbd_led_state.color.extra = colorcode;
			break;
		}
	}

	return size;
}

void clevo_keyboard_event_callb(u32 event);
void clevo_acpi_notify(struct acpi_device *device, u32 event);
void clevo_keyboard_write_state(void);

//

static uint param_color_left = KB_COLOR_DEFAULT;
module_param_named(color_left, param_color_left, uint, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(color_left, "Color for the Left Region");

static uint param_color_center = KB_COLOR_DEFAULT;
module_param_named(color_center, param_color_center, uint, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(color_center, "Color for the Center Region");

static uint param_color_right = KB_COLOR_DEFAULT;
module_param_named(color_right, param_color_right, uint, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(color_right, "Color for the Right Region");

static uint param_color_extra = KB_COLOR_DEFAULT;
module_param_named(color_extra, param_color_extra, uint, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(color_extra, "Color for the Extra Region");

static bool param_state = true;
module_param_named(state, param_state, bool, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(state,
		 "Set the State of the Keyboard TRUE = ON | FALSE = OFF");

static const struct kernel_param_ops param_ops_mode_ops = {
	.set = blinking_pattern_id_validator,
	.get = param_get_int,
};

static ushort param_blinking_pattern = DEFAULT_BLINKING_PATTERN;
module_param_cb(mode, &param_ops_mode_ops, &param_blinking_pattern, S_IRUGO);
MODULE_PARM_DESC(mode, "Set the keyboard backlight blinking pattern");

static const struct kernel_param_ops param_ops_brightness_ops = {
	.set = brightness_validator,
	.get = param_get_int,
};

static ushort param_brightness = 0xffff; // Default unset value (higher than max)
module_param_cb(brightness, &param_ops_brightness_ops, &param_brightness,
		S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(brightness, "Set the Keyboard Brightness");


// Param callback functions

static int blinking_pattern_id_validator(const char *value,
                                         const struct kernel_param *blinking_pattern_param)
{
	int blinking_pattern = 0;

	if (kstrtoint(value, 10, &blinking_pattern) != 0
	    || blinking_pattern < 0
	    || blinking_pattern > (ARRAY_SIZE(blinking_patterns) - 1)) {
		return -EINVAL;
	}

	return param_set_int(value, blinking_pattern_param);
}

static int brightness_validator(const char *value,
                                const struct kernel_param *brightness_param)
{
	int brightness = 0;

	pr_info("Setting brightness start %s", value);

	if (kstrtoint(value, 10, &brightness) != 0
	    || brightness < BRIGHTNESS_MIN
	    || brightness > BRIGHTNESS_MAX) {
		return -EINVAL;
	}
	return param_set_int(value, brightness_param);
}


// ATTR fs functions

static ssize_t show_brightness_fs(struct device *child,
				  struct device_attribute *attr, char *buffer)
{
	return sprintf(buffer, "%d\n", kbd_led_state.brightness);
}

static ssize_t set_brightness_fs(struct device *child,
                                 struct device_attribute *attr,
                                 const char *buffer, size_t size)
{
	unsigned int val;

	int err = kstrtouint(buffer, 0, &val);
	if (err) {
		return err;
	}

	val = clamp_t(u8, val, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
	
	if (kbd_led_state.mode == KB_TYPE_BW) {
		int ratio = BRIGHTNESS_MAX/BRIGHTNESS_MAX_BW;
		val = val / ratio;
	}
	
	set_brightness(val);

	return size;
}

static ssize_t show_state_fs(struct device *child,
			     struct device_attribute *attr, char *buffer)
{
	return sprintf(buffer, "%d\n", kbd_led_state.enabled);
}

static ssize_t set_state_fs(struct device *child, struct device_attribute *attr,
			    const char *buffer, size_t size)
{
	unsigned int state;

	int err = kstrtouint(buffer, 0, &state);
	if (err) {
		return err;
	}

	state = clamp_t(u8, state, 0, 1);

	set_enabled(state);

	return size;
}

static ssize_t show_color_left_fs(struct device *child,
				  struct device_attribute *attr, char *buffer)
{
	return sprintf(buffer, "%06x\n", kbd_led_state.color.left);
}

static ssize_t show_color_center_fs(struct device *child,
				    struct device_attribute *attr, char *buffer)
{
	return sprintf(buffer, "%06x\n", kbd_led_state.color.center);
}

static ssize_t show_color_right_fs(struct device *child,
				   struct device_attribute *attr, char *buffer)
{
	return sprintf(buffer, "%06x\n", kbd_led_state.color.right);
}

static ssize_t set_color_left_fs(struct device *child,
				 struct device_attribute *attr,
				 const char *color_string, size_t size)
{
	return set_color_string_region(color_string, size, REGION_LEFT);
}

static ssize_t set_color_center_fs(struct device *child,
				   struct device_attribute *attr,
				   const char *color_string, size_t size)
{
	return set_color_string_region(color_string, size, REGION_CENTER);
}

static ssize_t set_color_right_fs(struct device *child,
				  struct device_attribute *attr,
				  const char *color_string, size_t size)
{
	return set_color_string_region(color_string, size, REGION_RIGHT);
}

static DEVICE_ATTR(brightness, 0644, show_brightness_fs, set_brightness_fs);
static DEVICE_ATTR(state, 0644, show_state_fs, set_state_fs);
static DEVICE_ATTR(color_left, 0644, show_color_left_fs, set_color_left_fs);
static DEVICE_ATTR(color_center, 0644, show_color_center_fs, set_color_center_fs);
static DEVICE_ATTR(color_right, 0644, show_color_right_fs, set_color_right_fs);


static u32 clevo_wmi_evaluate_wmbb_method(u32 method_id, u32 arg,
	u32 *retval)
{
	struct acpi_buffer in  = { (acpi_size) sizeof(arg), &arg };
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	u32 tmp;

	pr_debug("%0#4x  IN : %0#6x\n", method_id, arg);

	status = wmi_evaluate_method(CLEVO_V1_GET_GUID, 0x00,
		method_id, &in, &out);

	if (unlikely(ACPI_FAILURE(status))) {
		goto exit;
	}

	obj = (union acpi_object *) out.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER) {
		tmp = (u32) obj->integer.value;
	}
	else {
		tmp = 0;
	}

	pr_debug("%0#4x  OUT: %0#6x (IN: %0#6x)\n", method_id, tmp, arg);

	if (likely(retval)) {
		*retval = tmp;
	}

exit:
	if (unlikely(ACPI_FAILURE(status)))
		return -EIO;
	
	return 0;
}

static u32 clevo_acpi_evaluate_method(struct acpi_device *device, u8 cmd, u32 arg, u32 *result)
{
	u32 status;
	acpi_handle handle;
	u64 dsm_rev_dummy = 0x00; // Dummy 0 value since not used
	u64 dsm_func = cmd;
	// Integer package data for argument
	union acpi_object dsm_argv4_package_data[] = {
		{.integer.type = ACPI_TYPE_INTEGER,
		 .integer.value = arg}};

	// Package argument
	union acpi_object dsm_argv4 = {
		.package.type = ACPI_TYPE_PACKAGE,
		.package.count = 1,
		.package.elements = dsm_argv4_package_data};

	union acpi_object *out_obj;

	guid_t clevo_acpi_dsm_uuid;

	status = guid_parse(CLEVO_ACPI_DSM_UUID, &clevo_acpi_dsm_uuid);
	if (status < 0)
		return -ENOENT;

	handle = acpi_device_handle(device);
	if (handle == NULL)
		return -ENODEV;

	pr_debug("evaluate _DSM cmd: %0#4x arg: %0#10x\n", cmd, arg);
	out_obj = acpi_evaluate_dsm(handle, &clevo_acpi_dsm_uuid, dsm_rev_dummy, dsm_func, &dsm_argv4);
	if (!out_obj)
	{
		pr_err("failed to evaluate _DSM\n");
		status = -1;
	}
	else
	{
		if (out_obj->type == ACPI_TYPE_INTEGER)
		{
			if (!IS_ERR_OR_NULL(result))
				*result = (u32)out_obj->integer.value;
		}
		else
		{
			pr_err("unknown output from _DSM\n");
			status = -ENODATA;
		}
	}

	ACPI_FREE(out_obj);

	return status;
}

static u32 clevo_evaluate_method(u32 cmd, u32 arg, u32 *result)
{
	u32 status;
	// using ACPI method call
	if (model == CLEVO_MODEL_V2) {
		status = clevo_acpi_evaluate_method(active_device, cmd, arg, result);
	}

	// using WMI method call
	if (model == CLEVO_MODEL_V1) {
		status = clevo_wmi_evaluate_wmbb_method( cmd, arg, result);
	}

	return status;
}

static void set_brightness(u8 brightness)
{
	if (kbd_led_state.mode == KB_TYPE_RGB) {
		if (!clevo_evaluate_method(WMI_SUBMETHOD_ID_SET_KB_LEDS, 0xF4000000 | brightness, NULL))
		{
			pr_info("Set rgb brightness to %d\n", brightness);
			kbd_led_state.brightness = brightness;
		}
	}
	
	if (kbd_led_state.mode == KB_TYPE_BW) {
		if (!clevo_evaluate_method(WMI_SUBMETHOD_ID_SET_KB_LEDS_BW, brightness, NULL)) {
			pr_info("Set brightness to %d\n", brightness);
			kbd_led_state.brightness = brightness;
		}
	}
}

static int set_color(u32 region, u32 color)
{
	if (kbd_led_state.mode == KB_TYPE_BW && region == REGION_LEFT) {
		set_brightness(color);
		return 0;
	}
	
	u32 cset =
		((color & 0x0000FF) << 16) | ((color & 0xFF0000) >> 8) |
		((color & 0x00FF00) >> 8);
	u32 wmi_submethod_arg = region | cset;

	// pr_info("Set Color '%08x' for region '%08x'", color, region);

	return clevo_evaluate_method(WMI_SUBMETHOD_ID_SET_KB_LEDS, wmi_submethod_arg, NULL);
}

static int set_color_code_region(u32 region, u32 colorcode)
{

	int err;

	if (0 == (err = set_color(region, colorcode)))
	{
		// after succesfully setting color, update our state struct
		// depending on which region was changed
		switch (region)
		{
		case REGION_LEFT:
			kbd_led_state.color.left = colorcode;
			break;
		case REGION_CENTER:
			kbd_led_state.color.center = colorcode;
			break;
		case REGION_RIGHT:
			kbd_led_state.color.right = colorcode;
			break;
		case REGION_EXTRA:
			kbd_led_state.color.extra = colorcode;
			break;
		}
	}
	return err;
}

static int set_next_color_whole_kb(void)
{
	/* "Calculate" new to-be color */
	u32 new_color_id;
	u32 new_color_code;

	new_color_id = kbd_led_state.whole_kbd_color + 1;
	if (new_color_id >= color_list.size)
	{
		new_color_id = 0;
	}
	new_color_code = color_list.colors[new_color_id].code;

	pr_info("set_next_color_whole_kb(): new_color_id: %i, new_color_code %X",
			new_color_id, new_color_code);

	/* Set color on all four regions*/
	set_color_code_region(REGION_LEFT, new_color_code);
	set_color_code_region(REGION_CENTER, new_color_code);
	set_color_code_region(REGION_RIGHT, new_color_code);
	set_color_code_region(REGION_EXTRA, new_color_code);

	kbd_led_state.whole_kbd_color = new_color_id;

	return 0;
}

static void set_blinking_pattern(u8 blinkling_pattern)
{
	pr_info("set_mode on %s", blinking_patterns[blinkling_pattern].name);

	if (!clevo_evaluate_method(WMI_SUBMETHOD_ID_SET_KB_LEDS, blinking_patterns[blinkling_pattern].value, NULL)) {
		// method was succesfull so update ur internal state struct
		kbd_led_state.blinking_pattern = blinkling_pattern;
	}

	if (blinkling_pattern == 0) {  // 0 is the "custom" blinking pattern

		// so just set all regions to the stored colors
		set_color(REGION_LEFT, kbd_led_state.color.left);
		set_color(REGION_CENTER, kbd_led_state.color.center);
		set_color(REGION_RIGHT, kbd_led_state.color.right);

		if (kbd_led_state.has_extra == 1) {
			set_color(REGION_EXTRA, kbd_led_state.color.extra);
		}
	}
}

static int set_enabled_cmd(u8 state)
{
	u32 cmd = 0xE0000000;
	pr_info("Set keyboard enabled to: %d\n", state);
	// pr_info("Has_extra: %d; Enabled %d; Brightness: %d; Blinking Pattern: %d; whole_kbd_color: %d;", kbd_led_state.has_extra, kbd_led_state.enabled, kbd_led_state.brightness, kbd_led_state.blinking_pattern, kbd_led_state.whole_kbd_color);

	if (state == 0)
	{
		cmd |= 0x003001;
	}
	else
	{
		cmd |= 0x07F001;
	}

	return clevo_evaluate_method(WMI_SUBMETHOD_ID_SET_KB_LEDS, cmd, NULL);
}

static void set_enabled(u8 state)
{
	if (!set_enabled_cmd(state))
	{
		kbd_led_state.enabled = state;
	}
}

void clevo_keyboard_event_callb(u32 event)
{
	//u32 key_event;

	pr_debug("event callback: (%0#10x)\n", event);

	//clevo_evaluate_method(0x01, 0, &key_event);

	//pr_info("event catched: (%0#6x)\n", key_event);

	switch (event)
	{
	case EVENT_CODE_DECREASE_BACKLIGHT_2:
	case EVENT_CODE_DECREASE_BACKLIGHT:
		if (kbd_led_state.mode == KB_TYPE_RGB) {
			if (kbd_led_state.brightness == BRIGHTNESS_MIN || (kbd_led_state.brightness - 25) < BRIGHTNESS_MIN) {
				set_brightness(BRIGHTNESS_MIN);
			}
			else {
				set_brightness(kbd_led_state.brightness - 25);
			}
		}

		if (kbd_led_state.mode == KB_TYPE_BW) {
			
			if (kbd_led_state.brightness > BRIGHTNESS_MIN) {
				kbd_led_state.brightness--;
			}
			set_brightness(kbd_led_state.brightness );

		}
		break;
	case EVENT_CODE_INCREASE_BACKLIGHT_2:
	case EVENT_CODE_INCREASE_BACKLIGHT:
		if (kbd_led_state.mode == KB_TYPE_RGB) {
			if (kbd_led_state.brightness == BRIGHTNESS_MAX || (kbd_led_state.brightness + 25) > BRIGHTNESS_MAX) {
				set_brightness(BRIGHTNESS_MAX);
			}
			else {
				set_brightness(kbd_led_state.brightness + 25);
			}
		}
		
		if (kbd_led_state.mode == KB_TYPE_BW) {
		
			kbd_led_state.brightness++;
			if (kbd_led_state.brightness > BRIGHTNESS_MAX_BW) {
				kbd_led_state.brightness = BRIGHTNESS_MAX_BW;
			}
			set_brightness(kbd_led_state.brightness);
		}
		break;

	case EVENT_CODE_NEXT_BLINKING_PATTERN:
		if (kbd_led_state.mode == KB_TYPE_RGB) {
			set_next_color_whole_kb();
		}
		break;

	case EVENT_CODE_TOGGLE_STATE_2:
	case EVENT_CODE_TOGGLE_STATE:
		if (kbd_led_state.mode == KB_TYPE_RGB) {
			set_enabled(kbd_led_state.enabled == 0 ? 1 : 0);
		}
		
		if (kbd_led_state.mode == KB_TYPE_BW) {
			set_brightness(kbd_led_state.brightness == 0 ? BRIGHTNESS_MAX_BW : 0);
		}
		break;

	default:
		pr_info("unmanaged event: (%0#10x)\n", event);
		break;
	}
}

static void clevo_wmi_notify(u32 value, void *context)
{
	u32 event;

	if (value != 0xD0) {
		pr_info("Unexpected WMI event (%0#6x)\n", value);
		return;
	}

	clevo_wmi_evaluate_wmbb_method(WMI_SUBMETHOD_ID_GET_EVENT, 0, &event);
	clevo_keyboard_event_callb(event);
}

static int clevo_acpi_add(struct acpi_device *device)
{
	u32 result;

	pr_info("%s",__PRETTY_FUNCTION__);

	active_device = device;

	// This is the get_app method for the keyboard, without it we would not get event notifications.
	clevo_acpi_evaluate_method(device, WMI_SUBMETHOD_ID_GET_AP, 0, &result);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static void clevo_acpi_remove(struct acpi_device *device)
{
	pr_info("%s",__PRETTY_FUNCTION__);

}
#else
static int clevo_acpi_remove(struct acpi_device *device)
{
	pr_info("%s",__PRETTY_FUNCTION__);
	return 0;
}
#endif

void clevo_acpi_notify(struct acpi_device *device, u32 event)
{
	 pr_info("ACPI event: %0#10x\n", event);
	clevo_keyboard_event_callb(event);
}

static const struct acpi_device_id device_ids[] = {
	{"CLV0001", 0},
	{"", 0},
};

MODULE_DEVICE_TABLE(acpi, device_ids);

static struct acpi_driver clevo_acpi_driver = {
	.name = "Clevo Keyboard",
	.class = "Clevo",
	.ids = device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add = clevo_acpi_add,
		.remove = clevo_acpi_remove,
		.notify = clevo_acpi_notify,
	},
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
	.owner = THIS_MODULE,
#endif	
};


void clevo_keyboard_write_state(void)
{
	// Note:
	// - set_blinking_pattern also writes colors
	// - set_brightness, set_enabled, set_blinking_pattern
	//   still also update state
	if (kbd_led_state.mode == KB_TYPE_RGB) {
		set_blinking_pattern(kbd_led_state.blinking_pattern);
		set_brightness(kbd_led_state.brightness);
		set_enabled(kbd_led_state.enabled);
	}

	if (kbd_led_state.mode == KB_TYPE_BW) {
		set_brightness(kbd_led_state.brightness);
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void clevo_platform_remove(struct platform_device *dev)
{
	pr_info("%s",__PRETTY_FUNCTION__);
	device_remove_file(&dev->dev, &dev_attr_brightness);
	device_remove_file(&dev->dev, &dev_attr_state);
	
}
#else
static int clevo_platform_remove(struct platform_device *dev)
{
	pr_info("%s",__PRETTY_FUNCTION__);
	device_remove_file(&dev->dev, &dev_attr_brightness);
	device_remove_file(&dev->dev, &dev_attr_state);
	
	return 0;
}
#endif

static int clevo_platform_suspend(struct platform_device *dev, pm_message_t state)
{
	
	if (kbd_led_state.mode == KB_TYPE_RGB) {
		// turning the keyboard off prevents default colours showing on resume
		set_enabled_cmd(0);
	}
	return 0;
}

static int clevo_platform_resume(struct platform_device *dev)
{

	clevo_evaluate_method(WMI_SUBMETHOD_ID_GET_AP, 0, NULL);

	clevo_keyboard_write_state();

	return 0;
}

static int clevo_platform_probe(struct platform_device *dev)
{
	if (device_create_file
	    (&dev->dev, &dev_attr_brightness) != 0) {
		pr_err
		    ("Sysfs attribute file creation failed for brightness\n");
	}
	if (device_create_file
	    (&dev->dev, &dev_attr_state) != 0) {
		pr_err
		    ("Sysfs attribute file creation failed for enabled\n");
	}
	if (device_create_file
	    (&dev->dev, &dev_attr_color_left) != 0) {
		pr_err
		    ("Sysfs attribute file creation failed for color left\n");
	}

	if (device_create_file
	    (&dev->dev, &dev_attr_color_center) != 0) {
		pr_err
		    ("Sysfs attribute file creation failed for color center\n");
	}

	if (device_create_file
	    (&dev->dev, &dev_attr_color_right) != 0) {
		pr_err
		    ("Sysfs attribute file creation failed for color right\n");
	}
	return 0;
}

static struct platform_driver platform_driver_clevo = {
	.remove = clevo_platform_remove,
	.suspend = clevo_platform_suspend,
	.resume = clevo_platform_resume,
	.probe = clevo_platform_probe,
	.driver =
		{
			.name = KBUILD_MODNAME,
			.owner = THIS_MODULE,
		},
};

static struct platform_device* platform_device_clevo;

static int __init clevo_platform_init(void)
{
	int result = 0;
	u32 event;

	pr_info("%s",__PRETTY_FUNCTION__);

	if (acpi_dev_found("CLV0001")) {
		pr_info("Clevo device found");

		if( wmi_has_guid(CLEVO_V1_EVENT_GUID) ) {
			pr_info("Using Clevo WMI");
			model = CLEVO_MODEL_V1;

			result = wmi_install_notify_handler(CLEVO_V1_EVENT_GUID,
				clevo_wmi_notify, NULL);
			if (unlikely(ACPI_FAILURE(result))) {
				pr_err("Could not register WMI notify handler (%0#6x)\n",
					result);
				return -EIO;
			}

			// Why is this needed? does it return something?
			clevo_wmi_evaluate_wmbb_method(WMI_SUBMETHOD_ID_GET_AP, 0, &event);
		}
		else {
			if( wmi_has_guid(CLEVO_V2_EVENT_GUID) ) {
				pr_info("Using Clevo ACPI");
				model = CLEVO_MODEL_V2;

				result = acpi_bus_register_driver(&clevo_acpi_driver);

				if (result < 0) {
					ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error registering driver\n"));
					return -ENODEV;
				}

			}
			else {
				pr_info("Unknown Clevo model");
			}
		}
	}
	else {
		pr_info("No Clevo device found");

		return -ENODEV;
	}

	result = platform_driver_register(&platform_driver_clevo);
	if (result < 0) {
		pr_err("Failed to create platform driver:%d",result);
		return -ENODEV;
	}

	platform_device_clevo = platform_device_alloc(KBUILD_MODNAME, -1);
	if (!platform_device_clevo) {
		pr_err("Failed to allocate device");
		goto error_device_alloc;
	}

	result = platform_device_add(platform_device_clevo);

	if (result) {
		pr_err("Failed to add device");
		goto error_device_add;
	}

	uint32_t value;
	uint32_t status = clevo_evaluate_method(WMI_SUBMETHOD_ID_GET_BIOS_1,0x00,&value);
	
	if (!status) {
		pr_info("Bios Feature register:%x\n",value);
		
		if ((value & 0x40000000) == 0) {
			kbd_led_state.mode = KB_TYPE_RGB;
			pr_info("RGB keyboard found\n");
		}
		else {
			pr_info("Single color keyboard found\n");
		}

	}
	// Init state from params
	
	kbd_led_state.color.left = param_color_left;
	kbd_led_state.color.center = param_color_center;
	kbd_led_state.color.right = param_color_right;
	kbd_led_state.color.extra = param_color_extra;
	if (kbd_led_state.mode == KB_TYPE_RGB) {
		if (param_brightness > BRIGHTNESS_MAX) param_brightness = BRIGHTNESS_DEFAULT;
	}
	
	if (kbd_led_state.mode == KB_TYPE_BW) {
		if (param_brightness > BRIGHTNESS_MAX_BW) param_brightness = BRIGHTNESS_DEFAULT_BW;
	}

	kbd_led_state.brightness = param_brightness;
	kbd_led_state.blinking_pattern = param_blinking_pattern;
	kbd_led_state.enabled = param_state;

	clevo_keyboard_write_state();
	
	/*
	pr_info("Has_extra: %d; Enabled %d; Brightness: %d; Blinking Pattern: %d; Color Pattern: %d; whole_kbd_color: %d;", kbd_led_state.has_extra, kbd_led_state.enabled, kbd_led_state.brightness, kbd_led_state.blinking_pattern, kbd_led_state.color.center, kbd_led_state.whole_kbd_color);
	*/
	
	return 0;

	error_device_add:

	platform_device_unregister(platform_device_clevo);

	error_device_alloc:

	platform_driver_unregister(&platform_driver_clevo);

	if (active_device) {
		acpi_bus_unregister_driver(&clevo_acpi_driver);
	}

	return -ENODEV;
}

static void __exit clevo_platform_exit(void)
{
	pr_info("%s",__PRETTY_FUNCTION__);
	platform_device_unregister(platform_device_clevo);
	platform_driver_unregister(&platform_driver_clevo);

	if (active_device) {
		acpi_bus_unregister_driver(&clevo_acpi_driver);
	}
	
	if (model == CLEVO_MODEL_V1) {
		wmi_remove_notify_handler(CLEVO_V1_EVENT_GUID);
	}
}

module_init(clevo_platform_init);
module_exit(clevo_platform_exit);
