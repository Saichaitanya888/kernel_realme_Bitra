// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[ADSP]([%s][%d]): " fmt, __func__, __LINE__

#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/iio/consumer.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/of_platform.h>

#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>

#include "oplus_hal_adsp.h"

#define OPLUS_HVDCP_DISABLE_INTERVAL round_jiffies_relative(msecs_to_jiffies(15000))
#define OPLUS_HVDCP_DETECT_TO_DETACH_TIME 100
#define OEM_MISC_CTL_DATA_PAIR(cmd, enable) ((enable ? 0x3 : 0x1) << cmd)

struct battery_chg_dev *g_bcdev = NULL;
static void oplus_vooc_status_func(struct work_struct *work);
static int oplus_otg_ap_enable(bool enable);
static int oplus_get_vchg_trig_status(void);
static bool oplus_vchg_trig_is_support(void);
extern void oplus_wake_up_usbtemp_thread(void);
extern void oplus_usb_set_none_role(void);
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
/*for p922x compile*/
void __attribute__((weak)) oplus_set_wrx_otg_value(void)
{
	return;
}
int __attribute__((weak)) oplus_get_idt_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_otg_val(void)
{
	return 0;
}
void __attribute__((weak)) oplus_wireless_set_otg_en_val(void)
{
	return;
}
void __attribute__((weak)) oplus_dcin_irq_enable(void)
{
	return;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static const int battery_prop_map[BATT_PROP_MAX] = {
	[BATT_STATUS]		= POWER_SUPPLY_PROP_STATUS,
	[BATT_HEALTH]		= POWER_SUPPLY_PROP_HEALTH,
	[BATT_PRESENT]		= POWER_SUPPLY_PROP_PRESENT,
	[BATT_CHG_TYPE]		= POWER_SUPPLY_PROP_CHARGE_TYPE,
	[BATT_CAPACITY]		= POWER_SUPPLY_PROP_CAPACITY,
	[BATT_VOLT_OCV]		= POWER_SUPPLY_PROP_VOLTAGE_OCV,
	[BATT_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[BATT_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[BATT_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[BATT_CHG_CTRL_LIM]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	[BATT_CHG_CTRL_LIM_MAX]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	[BATT_TEMP]		= POWER_SUPPLY_PROP_TEMP,
	[BATT_TECHNOLOGY]	= POWER_SUPPLY_PROP_TECHNOLOGY,
	[BATT_CHG_COUNTER]	= POWER_SUPPLY_PROP_CHARGE_COUNTER,
	[BATT_CYCLE_COUNT]	= POWER_SUPPLY_PROP_CYCLE_COUNT,
	[BATT_CHG_FULL_DESIGN]	= POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	[BATT_CHG_FULL]		= POWER_SUPPLY_PROP_CHARGE_FULL,
	[BATT_MODEL_NAME]	= POWER_SUPPLY_PROP_MODEL_NAME,
	[BATT_TTF_AVG]		= POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	[BATT_TTE_AVG]		= POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	[BATT_POWER_NOW]	= POWER_SUPPLY_PROP_POWER_NOW,
	[BATT_POWER_AVG]	= POWER_SUPPLY_PROP_POWER_AVG,
};

static const int usb_prop_map[USB_PROP_MAX] = {
	[USB_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[USB_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[USB_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[USB_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[USB_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
	[USB_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[USB_ADAP_TYPE]		= POWER_SUPPLY_PROP_USB_TYPE,
	[USB_TEMP]		= POWER_SUPPLY_PROP_TEMP,
};

static const int wls_prop_map[WLS_PROP_MAX] = {
	[WLS_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[WLS_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[WLS_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[WLS_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[WLS_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
};

/* Standard usb_type definitions similar to power_supply_sysfs.c */
static const char * const power_supply_usb_type_text[] = {
	"Unknown", "SDP", "DCP", "CDP", "ACA", "C",
	"PD", "PD_DRP", "PD_PPS", "BrickID"
};

/* Custom usb_type definitions */
static const char * const qc_power_supply_usb_type_text[] = {
	"HVDCP", "HVDCP_3", "HVDCP_3P5"
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static int oem_battery_chg_write(struct battery_chg_dev *bcdev, void *data,
	int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		chg_err("glink state is down\n");
		return -ENOTCONN;
	}

	mutex_lock(&bcdev->read_buffer_lock);
	reinit_completion(&bcdev->oem_read_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->oem_read_ack,
			msecs_to_jiffies(OEM_READ_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->read_buffer_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	/*chg_err("oem_battery_chg_write end\n");*/
	mutex_unlock(&bcdev->read_buffer_lock);

	return rc;
}

static int oem_read_buffer(struct battery_chg_dev *bcdev)
{
	struct oem_read_buffer_req_msg req_msg = { { 0 } };

	req_msg.data_size = sizeof(bcdev->read_buffer_dump.data_buffer);
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OPCODE_READ_BUFFER;

	/*chg_err("oem_read_buffer\n");*/

	return oem_battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

void oplus_get_props_from_adsp_by_buffer(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		pr_debug(KERN_ERR "!!!bcdev null, oplus_get_batt_argv_buffer\n");
		return;
	}
	oem_read_buffer(bcdev);
}

static void handle_oem_read_buffer(struct battery_chg_dev *bcdev,
	struct oem_read_buffer_resp_msg *resp_msg, size_t len)
{
	u32 buf_len;

	/*chg_err("correct length received: %zu expected: %u\n", len,
		sizeof(bcdev->read_buffer_dump));*/

	if (len > sizeof(bcdev->read_buffer_dump)) {
		chg_err("Incorrect length received: %zu expected: %u\n", len,
		sizeof(bcdev->read_buffer_dump));
		return;
	}

	buf_len = resp_msg->data_size;
	if (buf_len > sizeof(bcdev->read_buffer_dump.data_buffer)) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}

	/*chg_err("buf length: %u\n", buf_len);*/
	if (buf_len == 0) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}
	memcpy(bcdev->read_buffer_dump.data_buffer, resp_msg->data_buffer, buf_len);

	/*pr_debug(KERN_ERR "%s : ----temp[%d], current[%d], vol[%d], soc[%d], rm[%d], chg_cyc[%d], fcc[%d], cc[%d], soh[%d], \
		suspend[%d], oplus_UsbCommCapable[%d], oplus_pd_svooc[%d]", __func__,
		bcdev->read_buffer_dump.data_buffer[0], bcdev->read_buffer_dump.data_buffer[1], bcdev->read_buffer_dump.data_buffer[2],
		bcdev->read_buffer_dump.data_buffer[3], bcdev->read_buffer_dump.data_buffer[4], bcdev->read_buffer_dump.data_buffer[5],
		bcdev->read_buffer_dump.data_buffer[6], bcdev->read_buffer_dump.data_buffer[7], bcdev->read_buffer_dump.data_buffer[8],
		bcdev->read_buffer_dump.data_buffer[9], bcdev->read_buffer_dump.data_buffer[10], bcdev->read_buffer_dump.data_buffer[11]);*/

	if (bcdev->read_buffer_dump.data_buffer[9] == 0) {
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_SUSPEND_CHECK);
	}

	complete(&bcdev->oem_read_ack);
}
#endif

static int battery_chg_fw_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return -ENOTCONN;
	}

	reinit_completion(&bcdev->fw_buf_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->fw_buf_ack,
					msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			return -ETIMEDOUT;
		}

		rc = 0;
	}

	return rc;
}

static int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return 0;
	}

	if (bcdev->debug_battery_detected && bcdev->block_tx)
		return 0;

	mutex_lock(&bcdev->rw_lock);
	reinit_completion(&bcdev->ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ack,
					msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->rw_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bcdev->rw_lock);

	return rc;
}

static int write_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id, u32 val)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = val;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;

	pr_debug("psy: %s prop_id: %u val: %u\n", pst->psy->desc->name,
		req_msg.property_id, val);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = 0;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int get_property_id(struct psy_state *pst,
			enum power_supply_property prop)
{
	u32 i;

	for (i = 0; i < pst->prop_count; i++)
		if (pst->map[i] == prop)
			return i;

	chg_err("No property id for property %d in psy %s\n", prop,
		pst->psy->desc->name);

	return -ENOENT;
}

static void battery_chg_notify_enable(struct battery_chg_dev *bcdev)
{
	struct battery_charger_set_notify_msg req_msg = { { 0 } };
	int rc;

	/* Send request to enable notification */
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_NOTIFY;
	req_msg.hdr.opcode = BC_SET_NOTIFY_REQ;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0)
		chg_err("Failed to enable notification rc=%d\n", rc);
}

static void battery_chg_subsys_up_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, subsys_up_work);

	battery_chg_notify_enable(bcdev);
}

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_typec_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		pr_debug(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return;
	}

	pst = &bcdev->psy_list[PSY_TYPE_USB];

	/* set disable typec mode */
	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, TYPEC_PORT_ROLE_TRY_SNK);
	if (rc < 0) {
		pr_debug(KERN_ERR "[OPLUS_CHG][%s]: Couldn't write 0x2b44[3] rc=%d\n", __func__, rc);
	}
}

static void oplus_vooc_status_func(struct work_struct *work)
{
}

#define OTG_SKIN_TEMP_HIGH 450
#define OTG_SKIN_TEMP_MAX 540
int oplus_get_bat_info_for_otg_status_check(int *soc, int *ichaging)
{
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;
	int prop_id = 0;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return 0;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return -1;
	}
	*ichaging = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CAPACITY);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery soc fail, rc=%d\n", rc);
		return -1;
	}
	*soc = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);

	return 0;
}

static int oplus_oem_misc_ctl(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OEM_MISC_CTL, bcdev->oem_misc_ctl_data);
	if (rc)
		chg_err("oplus_oem_misc_ctl fail, rc=%d\n", rc);
	else
		chg_err("oem_misc_ctl_data: 0x%x\n", bcdev->oem_misc_ctl_data);

	return rc;
}

static void oplus_oem_lcm_en_check_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = g_bcdev;
	int enable, vph_track_high;
	static int last_enable = -1, last_vph_track_high = -1;

	if (!bcdev) {
		chg_err("chip or bcdev NULL\n");
		return;
	}

	enable = (bcdev->oem_usb_online ? 0 : 1);
	/* vph_track_high = (chip->batt_full ? 1 : 0); */
	vph_track_high = 0; /* TODO */

	if (bcdev->oem_usb_online && (enable == last_enable) && (last_vph_track_high == vph_track_high)) {
		schedule_delayed_work(&bcdev->oem_lcm_en_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
		return;
	}

	bcdev->oem_misc_ctl_data = 0;
	bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_LCM_EN, enable);
	bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_NCM_AUTO_MODE, enable);
	bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_VPH_TRACK_HIGH, vph_track_high);
	oplus_oem_misc_ctl();
	last_enable = enable;
	last_vph_track_high = vph_track_high;

	if (bcdev->oem_usb_online) {
		schedule_delayed_work(&bcdev->oem_lcm_en_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
	}
}

void oplus_turn_off_power_when_adsp_crash(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		pr_debug(KERN_ERR "!!!bcdev null\n");
		return;
	}

	bcdev->is_chargepd_ready = false;
	bcdev->pd_svooc = false;

	if (bcdev->otg_online == true) {
		bcdev->otg_online = false;
#ifdef OPLUS_CHG_UNDEF /* TODO */
		oplus_wpc_set_booster_en_val(0);
		oplus_wpc_set_ext1_wired_otg_en_val(0);
		oplus_wpc_set_wrx_en_value(0);
#endif
	}
}
EXPORT_SYMBOL(oplus_turn_off_power_when_adsp_crash);

bool oplus_is_pd_svooc(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		pr_debug(KERN_ERR "!!!bcdev null\n");
		return false;
	}

	pr_debug(KERN_ERR "!!!:%s, pd_svooc[%d]\n", __func__, bcdev->pd_svooc);
	return bcdev->pd_svooc;
}
EXPORT_SYMBOL(oplus_is_pd_svooc);

void oplus_adsp_crash_recover_work(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		pr_debug(KERN_ERR "!!!bcdev null\n");
		return;
	}

	/* schedule_delayed_work(&bcdev->adsp_crash_recover_work, round_jiffies_relative(msecs_to_jiffies(1500))); */
}
EXPORT_SYMBOL(oplus_adsp_crash_recover_work);

#ifdef OPLUS_CHG_UNDEF /* TODO */
static void oplus_adsp_crash_recover_func(struct work_struct *work)
{
	struct oplus_chg_chip *chip = get_oplus_chg_chip();
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!chip || !bcdev) {
		pr_debug(KERN_ERR "!!!chip or bcdev null\n");
		return;
	}

	if (chip->voocphy.fast_chg_type != FASTCHG_CHARGER_TYPE_UNKOWN) {
		chip->voocphy.fastchg_dummy_start = true;
	}
	chip->voocphy.fastchg_ing = false;
	chip->voocphy.fastchg_start = false;
	chip->voocphy.fastchg_to_normal = false;
	chip->voocphy.fastchg_to_warm = false;
	chip->charger_type  = POWER_SUPPLY_TYPE_UNKNOWN;
	oplus_voocphy_enable(true);
	schedule_delayed_work(&bcdev->otg_init_work, 0);
	oplus_chg_wake_update_work();
	schedule_delayed_work(&bcdev->voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(0)));
	schedule_delayed_work(&bcdev->check_charger_out_work, round_jiffies_relative(msecs_to_jiffies(3000)));
}

static void oplus_chg_clear_voocphy_status(struct oplus_chg_chip *chip)
{
	chip->voocphy.fastchg_dummy_start = false;
	chip->voocphy.fastchg_ing = false;
	chip->voocphy.fastchg_start = false;
	chip->voocphy.fastchg_to_normal = false;
	chip->voocphy.fastchg_to_warm = false;
	chip->voocphy.fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
}
static void oplus_check_charger_out_func(struct work_struct *work)
{
	int chg_vol = 0;
	struct oplus_chg_chip *chip = get_oplus_chg_chip();
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst_batt = NULL;

	if (!chip || !bcdev) {
		pr_debug(KERN_ERR "!!!chip or bcdev null\n");
		return;
	}

	pst_batt = &bcdev->psy_list[PSY_TYPE_BATTERY];

	chg_vol = oplus_chg_get_charger_voltage();
	if (chg_vol >= 0 && chg_vol < 2000) {
		oplus_chg_clear_voocphy_status(chip);
		if (pst_batt->psy)
			power_supply_changed(pst_batt->psy);
		pr_debug(KERN_ERR, "charger out, chg_vol:%d\n", chg_vol);
	}
}

static void oplus_voocphy_enable_check_func(struct work_struct *work)
{
	int rc = 0;
	int voocphy_enable = 0;
	struct oplus_chg_chip *chip = get_oplus_chg_chip();
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!chip || !bcdev) {
		pr_debug(KERN_ERR "!!!chip or bcdev null\n");
		return;
	}

	if (chip->mmi_chg == 0 || chip->charger_exist == false
		|| chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		/*chg_err("is_mmi_chg no_charger_exist no_dcp_type\n");*/
		schedule_delayed_work(&bcdev->voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
		return;
	}

	voocphy_enable = oplus_get_voocphy_enable();
	if (voocphy_enable == 0) {
		chg_err("!!!need enable voocphy again\n");
		rc = oplus_voocphy_enable(true);
		schedule_delayed_work(&bcdev->voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(500)));
	} else {
		/*chg_err("!!!enable voocphy ok\n");*/
		schedule_delayed_work(&bcdev->voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
	}
}
#endif

#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifdef OPLUS_CHG_UNDEF
static void oplus_wait_wired_charge_on_work(struct work_struct *work)
{
	pr_debug(KERN_ERR "[OPLUS_CHG][%s]<~WPC~> wait_wired_charge_on\n", __func__);
	oplus_wpc_set_wrx_en_value(0);
	oplus_wpc_set_wls_pg_value(1);
	msleep(100);
	oplus_wpc_set_booster_en_val(1);
	oplus_wpc_set_ext2_wireless_otg_en_val(1);
	msleep(100);
	oplus_wpc_set_tx_start();
	return;
}

static void oplus_switch_to_wired_charge(struct battery_chg_dev *bcdev)
{
	oplus_wpc_dis_wireless_chg(1);
	if (oplus_wpc_get_wireless_charge_start() == true) {
		/*oplus_wpc_dis_wireless_chg(1);*/
		oplus_wpc_set_vbat_en_val(0);
		msleep(100);
		oplus_wpc_set_wrx_en_value(0);
		oplus_wpc_set_wls_pg_value(1);
	}

	if (oplus_wpc_get_otg_charging()) {
		/*oplus_wpc_dis_wireless_chg(1);*/
		mp2650_wireless_set_mps_otg_en_val(0);
		oplus_wpc_set_wrx_otg_en_value(0);

		cancel_delayed_work_sync(&bcdev->wait_wired_charge_on);
		schedule_delayed_work(&bcdev->wait_wired_charge_on, msecs_to_jiffies(100));
	}
}

static void oplus_wait_wired_charge_off_work(struct work_struct *work)
{
	pr_debug(KERN_ERR "[OPLUS_CHG][%s]<~WPC~> wait_wired_charge_off\n", __func__);
	oplus_wpc_dis_wireless_chg(0);
	oplus_wpc_set_rtx_function_prepare();
	oplus_wpc_set_rtx_function(true);
	return;
}

static void oplus_switch_from_wired_charge(struct battery_chg_dev *bcdev)
{
	if (oplus_wpc_get_otg_charging()) {
		oplus_wpc_set_booster_en_val(0);
		oplus_wpc_set_ext2_wireless_otg_en_val(0);
		oplus_wpc_set_wls_pg_value(0);
		cancel_delayed_work_sync(&bcdev->wait_wired_charge_off);
		schedule_delayed_work(&bcdev->wait_wired_charge_off, msecs_to_jiffies(100));
	} else {
		if (oplus_wpc_get_fw_updating() == false)
			oplus_wpc_dis_wireless_chg(0);
	}
}
#endif

bool oplus_get_wired_otg_online(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return false;
	}

	if (bcdev->wls_fw_update == true)
		return false;
	return bcdev->otg_online;
}

bool oplus_get_wired_chg_present(void)
{
	if (oplus_get_wired_otg_online() == true)
		return false;
	if (oplus_vchg_trig_is_support() == true && oplus_get_vchg_trig_status() == 0)
		return true;
	return false;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static void battery_chg_state_cb(void *priv, enum pmic_glink_state state)
{
	struct battery_chg_dev *bcdev = priv;

	pr_debug("state: %d\n", state);

	atomic_set(&bcdev->state, state);
	if (state == PMIC_GLINK_STATE_UP)
		schedule_work(&bcdev->subsys_up_work);
}

/**
 * qti_battery_charger_get_prop() - Gets the property being requested
 *
 * @name: Power supply name
 * @prop_id: Property id to be read
 * @val: Pointer to value that needs to be updated
 *
 * Return: 0 if success, negative on error.
 */
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	struct power_supply *psy;
	struct battery_chg_dev *bcdev;
	struct psy_state *pst;
	int rc = 0;

	if (prop_id >= BATTERY_CHARGER_PROP_MAX)
		return -EINVAL;

	if (strcmp(name, "battery") && strcmp(name, "usb") &&
	    strcmp(name, "wireless"))
		return -EINVAL;

	psy = power_supply_get_by_name(name);
	if (!psy)
		return -ENODEV;

#ifndef OPLUS_FEATURE_CHG_BASIC
	bcdev = power_supply_get_drvdata(psy);
#else
	bcdev = g_bcdev;
#endif
	if (!bcdev)
		return -ENODEV;

	power_supply_put(psy);

	switch (prop_id) {
	case BATTERY_RESISTANCE:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
		if (!rc)
			*val = pst->prop[BATT_RESISTANCE];
		break;
	default:
		break;
	}

	return rc;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static bool validate_message(struct battery_charger_resp_msg *resp_msg,
				size_t len)
{
	if (len != sizeof(*resp_msg)) {
		chg_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code) {
		chg_err("Error in response for opcode %#x prop_id %u, rc=%d\n",
			resp_msg->hdr.opcode, resp_msg->property_id,
			(int)resp_msg->ret_code);
		return false;
	}

	return true;
}

#define MODEL_DEBUG_BOARD	"Debug_Board"
static void handle_message(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_resp_msg *resp_msg = data;
	struct battery_model_resp_msg *model_resp_msg = data;
	struct wireless_fw_check_resp *fw_check_msg;
	struct wireless_fw_push_buf_resp *fw_resp_msg;
	struct wireless_fw_update_status *fw_update_msg;
	struct wireless_fw_get_version_resp *fw_ver_msg;
	struct psy_state *pst;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case BC_BATTERY_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

		/* Handle model response uniquely as it's a string */
		if (pst->model && len == sizeof(*model_resp_msg)) {
			memcpy(pst->model, model_resp_msg->model, MAX_STR_LEN);
			ack_set = true;
			bcdev->debug_battery_detected = !strcmp(pst->model,
					MODEL_DEBUG_BOARD);
			break;
		}

		/* Other response should be of same type as they've u32 value */
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		if (validate_message(data, len))
			ack_set = true;

		break;
	case BC_SET_NOTIFY_REQ:
		/* Always ACK response for notify request */
		ack_set = true;
		break;
	case BC_WLS_FW_CHECK_UPDATE:
		if (len == sizeof(*fw_check_msg)) {
			fw_check_msg = data;
			if (fw_check_msg->ret_code == 1)
				bcdev->wls_fw_update_reqd = true;
			ack_set = true;
		} else {
			chg_err("Incorrect response length %zu for wls_fw_check_update\n",
				len);
		}
		break;
	case BC_WLS_FW_PUSH_BUF_RESP:
		if (len == sizeof(*fw_resp_msg)) {
			fw_resp_msg = data;
			if (fw_resp_msg->fw_update_status == 1)
				complete(&bcdev->fw_buf_ack);
		} else {
			chg_err("Incorrect response length %zu for wls_fw_push_buf_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_UPDATE_STATUS_RESP:
		if (len == sizeof(*fw_update_msg)) {
			fw_update_msg = data;
			if (fw_update_msg->fw_update_done == 1)
				complete(&bcdev->fw_update_ack);
		} else {
			chg_err("Incorrect response length %zu for wls_fw_update_status_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_GET_VERSION:
		if (len == sizeof(*fw_ver_msg)) {
			fw_ver_msg = data;
			bcdev->wls_fw_version = fw_ver_msg->fw_version;
			ack_set = true;
		} else {
			chg_err("Incorrect response length %zu for wls_fw_get_version\n",
				len);
		}
		break;
	default:
		chg_err("Unknown opcode: %u\n", resp_msg->hdr.opcode);
		break;
	}

	if (ack_set)
		complete(&bcdev->ack);
}

static struct power_supply_desc usb_psy_desc;

static void battery_chg_update_usb_type_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, usb_type_work);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0) {
		chg_err("Failed to read USB_ADAP_TYPE rc=%d\n", rc);
		return;
	}

	pr_debug("usb_adap_type: %u\n", pst->prop[USB_ADAP_TYPE]);

	switch (pst->prop[USB_ADAP_TYPE]) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_PD;
		break;
	default:
#ifdef OPLUS_FEATURE_CHG_BASIC
		rc = read_property_id(bcdev, pst, USB_ONLINE);
		if (rc < 0) {
			chg_err("Failed to read USB_ONLINE rc=%d\n", rc);
			return;
		}
		if (pst->prop[USB_ONLINE] == 0) {
			/*chg_err("lizhijie USB_ONLINE 00000\n");*/
			usb_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
		}
#else
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
#endif
		break;
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
#endif
}

static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_notify_msg *notify_msg = data;
	struct psy_state *pst = NULL;

	if (len != sizeof(*notify_msg)) {
		chg_err("Incorrect response length %zu\n", len);
		return;
	}

	pr_debug("notification: %#x\n", notify_msg->notification);

	switch (notify_msg->notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		schedule_work(&bcdev->usb_type_work);
		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		break;
#ifdef OPLUS_FEATURE_CHG_BASIC
	case BC_PD_SVOOC:
#ifdef OPLUS_CHG_UNDEF /* TODO */
		if ((get_oplus_chg_chip() && get_oplus_chg_chip()->wireless_support == false)
			|| oplus_get_wired_chg_present() == true) {
			pr_debug(KERN_ERR "!!!:%s, should set pd_svooc\n", __func__);
			oplus_usb_set_none_role();
			bcdev->pd_svooc = true;
		}
#endif
		pr_debug(KERN_ERR "!!!:%s, pd_svooc[%d]\n", __func__, bcdev->pd_svooc);
		break;
	case BC_VOOC_STATUS_GET:
		break;
	case BC_OTG_ENABLE:
		pr_debug(KERN_ERR "!!!!!enable otg\n");
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = true;
		bcdev->pd_svooc = false;
		break;
	case BC_OTG_DISABLE:
		pr_debug(KERN_ERR "!!!!!disable otg\n");
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = false;
		break;
	case BC_VOOC_VBUS_ADC_ENABLE:
		pr_debug(KERN_ERR "!!!!!vooc_vbus_adc_enable\n");
		bcdev->voocphy_err_check = true;
		break;
	case BC_CID_DETECT:
		pr_debug(KERN_ERR "!!!!!cid detect\n");
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CC_DETECT);
		break;
	case BC_QC_DETECT:
		bcdev->hvdcp_detect_ok = true;
		bcdev->hvdcp_detect_time = cpu_clock(smp_processor_id()) / 1000000;
		pr_debug(KERN_ERR " HVDCP2 detect: %d, the detect time: %lu\n",
			bcdev->hvdcp_detect_ok, bcdev->hvdcp_detect_time);
		break;
	case BC_TYPEC_STATE_CHANGE:
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CC_CHANGED);
		break;
#endif
	default:
		break;
	}

	if (pst && pst->psy) {
		/*
		 * For charger mode, keep the device awake at least for 50 ms
		 * so that device won't enter suspend when a non-SDP charger
		 * is removed. This would allow the userspace process like
		 * "charger" to be able to read power supply uevents to take
		 * appropriate actions (e.g. shutting down when the charger is
		 * unplugged).
		 */
		power_supply_changed(pst->psy);
		pm_wakeup_dev_event(bcdev->dev, 50, true);
	}
}

static int battery_chg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct battery_chg_dev *bcdev = priv;

	pr_debug("owner: %u type: %u opcode: %#x len: %zu\n", hdr->owner,
		hdr->type, hdr->opcode, len);

	if (!bcdev->is_chargepd_ready)
		bcdev->is_chargepd_ready = true;

	if (hdr->opcode == BC_NOTIFY_IND)
		handle_notification(bcdev, data, len);
#ifdef OPLUS_FEATURE_CHG_BASIC
	else if (hdr->opcode == OEM_OPCODE_READ_BUFFER)
		handle_oem_read_buffer(bcdev, data, len);
#endif
	else
		handle_message(bcdev, data, len);

	return 0;
}

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];

	return 0;
}

static int wls_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	return 0;
}

static int wls_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return 0;
}

static enum power_supply_property wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
#ifdef OPLUS_FEATURE_CHG_BASIC
	POWER_SUPPLY_PROP_PRESENT,
#endif
};

static const struct power_supply_desc wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= wls_props,
	.num_properties		= ARRAY_SIZE(wls_props),
	.get_property		= wls_psy_get_prop,
	.set_property		= wls_psy_set_prop,
	.property_is_writeable	= wls_psy_prop_is_writeable,
};

static const char *get_usb_type_name(u32 usb_type)
{
	u32 i;

	if (usb_type >= QTI_POWER_SUPPLY_USB_TYPE_HVDCP &&
	    usb_type <= QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5) {
		for (i = 0; i < ARRAY_SIZE(qc_power_supply_usb_type_text);
		     i++) {
			if (i == (usb_type - QTI_POWER_SUPPLY_USB_TYPE_HVDCP))
				return qc_power_supply_usb_type_text[i];
		}
		return "Unknown";
	}

	for (i = 0; i < ARRAY_SIZE(power_supply_usb_type_text); i++) {
		if (i == usb_type)
			return power_supply_usb_type_text[i];
	}

	return "Unknown";
}

#ifndef OPLUS_FEATURE_CHG_BASIC
static int usb_psy_set_icl(struct battery_chg_dev *bcdev, u32 prop_id, int val)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	u32 temp;
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0)
		return rc;

	/* Allow this only for SDP or USB_PD and not for other charger types */
	if (pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_SDP &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD)
		return -EINVAL;

	/*
	 * Input current limit (ICL) can be set by different clients. E.g. USB
	 * driver can request for a current of 500/900 mA depending on the
	 * port type. Also, clients like EUD driver can pass 0 or -22 to
	 * suspend or unsuspend the input for its use case.
	 */

	temp = val;
	if (val < 0)
		temp = UINT_MAX;

	rc = write_property_id(bcdev, pst, prop_id, temp);
	if (!rc)
		pr_debug("Set ICL to %u\n", temp);

	return rc;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifdef OPLUS_CHG_UNDEF /* TODO */
void oplus_chg_set_curr_level_to_voocphy(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = get_oplus_chg_chip();
	int cool_down = 0;

	if (!chip || !bcdev) {
		chg_err("chip or bcdev is NULL!\n");
		return;
	}
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	cool_down = oplus_chg_get_cool_down_status();
	rc = write_property_id(bcdev, pst, BATT_SET_COOL_DOWN, cool_down);
	if (rc) {
		chg_err("set curr level fail, rc=%d\n", rc);
		return;
	}

	chg_debug("ap set curr level[%d] to voocphy\n", cool_down);
}

void oplus_chg_set_match_temp_to_voocphy(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = get_oplus_chg_chip();
	int match_temp = 0;

	if (!chip || !bcdev) {
		chg_err("chip or bcdev is NULL!\n");
		return;
	}
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	match_temp = oplus_chg_match_temp_for_chging();
	rc = write_property_id(bcdev, pst, BATT_SET_MATCH_TEMP, match_temp);
	if (rc) {
		chg_err("set match temp fail, rc=%d\n", rc);
		return;
	}

	/*chg_debug("ap set match temp[%d] to voocphy\n", match_temp);*/
}
#endif
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC

#ifdef OPLUS_CHG_UNDEF
static unsigned int get_chg_ctl_param_info(struct battery_chg_dev *bcdev)
{
	struct psy_state *pst = NULL;
	int rc = 0;
	int intval = 0;
	unsigned int project = 0, index = 0;

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_VOOC_CHG_PARAM_INFO);
	if (rc < 0) {
		pr_debug(KERN_ERR "!!![voocphy] read USB_VOOC_CHG_PARAM_INFO fail\n");
		return 0;
	}
	intval = pst->prop[USB_VOOC_CHG_PARAM_INFO];
	index = (intval & 0xFF);
	project = ((intval >> 8) & 0xFFFFFF);
	return (project * 100 + index);
}

static void oplus_vooc_handle_voocphy_status(void)
{
	struct oplus_chg_chip *chip = get_oplus_chg_chip();
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;
	struct psy_state *pst_batt = NULL;
	int rc = 0;
	int intval = 0;

	if (!chip || !bcdev) {
		pr_debug(KERN_ERR "!!!chip or bcdev null, voocphy non handle status: [%d]\n", intval);
		return;
	}

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	pst_batt = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = read_property_id(bcdev, pst, USB_VOOCPHY_STATUS);
	if (rc < 0) {
		pr_debug(KERN_ERR "!!![voocphy] read voocphy_status fail\n");
		return;
	}
	intval = pst->prop[USB_VOOCPHY_STATUS];

	chip->voocphy.chg_ctl_param_info = get_chg_ctl_param_info(bcdev);


	pr_debug(KERN_ERR "!!![voocphy] vooc status: [%d]\n", intval);
	chip->voocphy.receive_data = (intval & 0xFF);
	if ((intval & 0xFF) == FAST_NOTIFY_PRESENT) {
		chip->voocphy.fastchg_start = true;
		chip->voocphy.fastchg_to_warm = false;
		chip->voocphy.fastchg_dummy_start = false;
		chip->voocphy.fastchg_to_normal = false;
		chip->voocphy.fastchg_ing = false;
		chip->voocphy.fast_chg_type = ((intval >> 8) & 0x7F);
		pr_debug(KERN_ERR "!!![voocphy] fastchg start: [%d], adapter version: [0x%0x]\n",
			chip->voocphy.fastchg_start, chip->voocphy.fast_chg_type);
		if (pst_batt->psy)
			power_supply_changed(pst_batt->psy);
		oplus_chg_set_curr_level_to_voocphy();
	} else if ((intval & 0xFF) == FAST_NOTIFY_DUMMY_START) {
		chip->voocphy.fastchg_start = false;
		chip->voocphy.fastchg_to_warm = false;
		chip->voocphy.fastchg_dummy_start = true;
		chip->voocphy.fastchg_to_normal = false;
		chip->voocphy.fastchg_ing = false;
		chip->voocphy.fast_chg_type = ((intval >> 8) & 0x7F);
		pr_debug(KERN_ERR "!!![voocphy] fastchg dummy start: [%d], adapter version: [0x%0x]\n",
				chip->voocphy.fastchg_dummy_start, chip->voocphy.fast_chg_type);
		if (pst_batt->psy)
			power_supply_changed(pst_batt->psy);
	} else if ((intval & 0xFF) == FAST_NOTIFY_ONGOING) {
		chip->voocphy.fastchg_ing = true;
		pr_debug(KERN_ERR "!!!! voocphy fastchg ongoing\n");
		oplus_chg_set_curr_level_to_voocphy();
		if (chip->voocphy.fast_chg_type == FASTCHG_CHARGER_TYPE_UNKOWN) {
			chip->voocphy.fastchg_start = true;
			rc = read_property_id(bcdev, pst, USB_VOOC_FAST_CHG_TYPE);
			if (rc < 0) {
				pr_debug(KERN_ERR "!!![voocphy] read fastchg type fail\n");
				return;
			}
			chip->voocphy.fast_chg_type = (pst->prop[USB_VOOC_FAST_CHG_TYPE]) & 0x7F;

			if (pst_batt->psy)
				power_supply_changed(pst_batt->psy);
		}
		if (chip->chg_ops->get_charging_enable() == true) {
			oplus_chg_disable_charge();
			oplus_chg_suspend_charger();
		}
	} else if ((intval & 0xFF) == FAST_NOTIFY_FULL || (intval & 0xFF) == FAST_NOTIFY_BAD_CONNECTED) {
		chip->voocphy.fastchg_start = false;
		chip->voocphy.fastchg_to_warm = false;
		chip->voocphy.fastchg_dummy_start = false;
		chip->voocphy.fastchg_to_normal = true;
		chip->voocphy.fastchg_ing = false;
		pr_debug(KERN_ERR "!!![voocphy] fastchg to normal: [%d]\n",
			chip->voocphy.fastchg_to_normal);
		if (pst_batt->psy)
			power_supply_changed(pst_batt->psy);
	} else if ((intval & 0xFF) == FAST_NOTIFY_BATT_TEMP_OVER) {
		chip->voocphy.fastchg_start = false;
		chip->voocphy.fastchg_to_warm = true;
		chip->voocphy.fastchg_dummy_start = false;
		chip->voocphy.fastchg_to_normal = false;
		chip->voocphy.fastchg_ing = false;
		pr_debug(KERN_ERR "!!![voocphy] fastchg to warm: [%d]\n",
				chip->voocphy.fastchg_to_warm);
		if (pst_batt->psy)
			power_supply_changed(pst_batt->psy);
	} else if ((intval & 0xFF) == FAST_NOTIFY_ERR_COMMU) {
		chip->voocphy.fastchg_start = false;
		chip->voocphy.fastchg_to_warm = false;
		chip->voocphy.fastchg_dummy_start = false;
		chip->voocphy.fastchg_to_normal = false;
		chip->voocphy.fastchg_ing = false;
		chip->voocphy.fast_chg_type = 0;
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_chg_voocphy_err();
		oplus_chg_wake_update_work();
		pr_debug(KERN_ERR "!!![voocphy] fastchg err commu: [%d]\n", intval);
	} else if ((intval & 0xFF) == FAST_NOTIFY_SWITCH_TEMP_RANGE) {
		chip->voocphy.fastchg_start = false;
		chip->voocphy.fastchg_to_warm = false;
		chip->voocphy.fastchg_dummy_start = true;
		chip->voocphy.fastchg_to_normal = false;
		chip->voocphy.fastchg_ing = false;
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		if (pst_batt->psy) {
			power_supply_changed(pst_batt->psy);
		}
		pr_debug(KERN_ERR "!!![voocphy] fastchg switch temp range: [%d]\n", intval);
	} else if ((intval & 0xFF) == FAST_NOTIFY_COMMU_CLK_ERR) {
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		pr_debug(KERN_ERR "!!![voocphy] fastchg commu clk err: [%d]\n", intval);
	} else if ((intval & 0xFF) == FAST_NOTIFY_HW_VBATT_HIGH
		|| (intval & 0xFF) == FAST_NOTIFY_HW_TBATT_HIGH) {
		oplus_chg_voocphy_err();
		oplus_chg_wake_update_work();
		pr_debug(KERN_ERR "!!![voocphy] fastchg hw vbatt || tbatt high: [%d]\n", intval);
	} else if ((intval & 0xFF) == FAST_NOTIFY_COMMU_TIME_OUT) {
		oplus_chg_voocphy_err();
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_chg_wake_update_work();
		pr_debug(KERN_ERR "!!![voocphy] fastchg timeout: [%d]\n", intval);
	} else {
		oplus_chg_voocphy_err();
		oplus_chg_wake_update_work();
		pr_debug(KERN_ERR "!!![voocphy] non handle status: [%d]\n", intval);
	}

	if ((intval & 0xFF) != FAST_NOTIFY_PRESENT) {
		if (bcdev->voocphy_err_check == true) {
			cancel_delayed_work_sync(&bcdev->voocphy_err_work);
		}
		bcdev->voocphy_err_check = false;
	}
}
#endif
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];
	if (prop == POWER_SUPPLY_PROP_TEMP)
		pval->intval = DIV_ROUND_CLOSEST((int)pval->intval, 10);

	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifdef OPLUS_CHG_UNDEF /* TODO */
int oplus_get_fast_chg_type(void)
{
	int fast_chg_type = 0;

	fast_chg_type = oplus_vooc_get_fast_chg_type();
	if (fast_chg_type == 0) {
		fast_chg_type = oplus_chg_get_charger_subtype();
	}
	if (fast_chg_type == 0) {
		if (oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_VOOC
			|| oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_SVOOC)
			fast_chg_type = oplus_wpc_get_adapter_type();
	}

	return fast_chg_type;
}
#endif
#endif

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc = 0;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
#ifndef OPLUS_FEATURE_CHG_BASIC
		rc = usb_psy_set_icl(bcdev, prop_id, pval->intval);
#endif
		break;
	default:
		break;
	}

	return rc;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_usb_type usb_psy_supported_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

static struct power_supply_desc usb_psy_desc = {
	.name			= "usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= usb_props,
	.num_properties		= ARRAY_SIZE(usb_props),
	.get_property		= usb_psy_get_prop,
	.set_property		= usb_psy_set_prop,
	.usb_types		= usb_psy_supported_types,
	.num_usb_types		= ARRAY_SIZE(usb_psy_supported_types),
	.property_is_writeable	= usb_psy_prop_is_writeable,
};

static int __battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					u32 fcc_ua)
{
	int rc;

	if (bcdev->restrict_chg_en) {
		fcc_ua = min_t(u32, fcc_ua, bcdev->restrict_fcc_ua);
		fcc_ua = min_t(u32, fcc_ua, bcdev->thermal_fcc_ua);
	}

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHG_CTRL_LIM, fcc_ua);
	if (rc < 0)
		chg_err("Failed to set FCC %u, rc=%d\n", fcc_ua, rc);
	else
		pr_debug("Set FCC to %u uA\n", fcc_ua);

	return rc;
}

static int battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					int val)
{
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (!bcdev->num_thermal_levels)
		return 0;

	if (bcdev->num_thermal_levels < 0) {
		chg_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val > bcdev->num_thermal_levels)
		return -EINVAL;

	fcc_ua = bcdev->thermal_levels[val];
	prev_fcc_ua = bcdev->thermal_fcc_ua;
	bcdev->thermal_fcc_ua = fcc_ua;

	rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
	if (!rc)
		bcdev->curr_thermal_level = val;
	else
		bcdev->thermal_fcc_ua = prev_fcc_ua;

	return rc;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = pst->model;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		pval->intval = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);
		if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) &&
		   (bcdev->fake_soc >= 0 && bcdev->fake_soc <= 100))
			pval->intval = bcdev->fake_soc;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		pval->intval = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = bcdev->curr_thermal_level;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		pval->intval = bcdev->num_thermal_levels;
		break;
	default:
		pval->intval = pst->prop[prop_id];
		break;
	}

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(bcdev, pval->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
#ifdef OPLUS_FEATURE_CHG_BASIC
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
#endif
};

static const struct power_supply_desc batt_psy_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= battery_props,
	.num_properties		= ARRAY_SIZE(battery_props),
	.get_property		= battery_psy_get_prop,
	.set_property		= battery_psy_set_prop,
	.property_is_writeable	= battery_psy_prop_is_writeable,
};

__maybe_unused static int battery_chg_init_psy(struct battery_chg_dev *bcdev)
{
	struct power_supply_config psy_cfg = {};
	int rc;

	psy_cfg.drv_data = bcdev;
	psy_cfg.of_node = bcdev->dev->of_node;
	bcdev->psy_list[PSY_TYPE_BATTERY].psy =
		devm_power_supply_register(bcdev->dev, &batt_psy_desc,
						&psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy);
		chg_err("Failed to register battery power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_USB].psy =
		devm_power_supply_register(bcdev->dev, &usb_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_USB].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_USB].psy);
		chg_err("Failed to register USB power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_WLS].psy =
		devm_power_supply_register(bcdev->dev, &wls_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy);
		chg_err("Failed to register wireless power supply, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int wireless_fw_send_firmware(struct battery_chg_dev *bcdev,
					const struct firmware *fw)
{
	struct wireless_fw_push_buf_req msg = {};
	const u8 *ptr;
	u32 i, num_chunks, partial_chunk_size;
	int rc;

	num_chunks = fw->size / WLS_FW_BUF_SIZE;
	partial_chunk_size = fw->size % WLS_FW_BUF_SIZE;

	if (!num_chunks)
		return -EINVAL;

	pr_debug("Updating FW...\n");

	ptr = fw->data;
	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_WLS_FW_PUSH_BUF_REQ;

	for (i = 0; i < num_chunks; i++, ptr += WLS_FW_BUF_SIZE) {
		msg.fw_chunk_id = i + 1;
		memcpy(msg.buf, ptr, WLS_FW_BUF_SIZE);

		pr_debug("sending FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	if (partial_chunk_size) {
		msg.fw_chunk_id = i + 1;
		memset(msg.buf, 0, WLS_FW_BUF_SIZE);
		memcpy(msg.buf, ptr, partial_chunk_size);

		pr_debug("sending partial FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wireless_fw_check_for_update(struct battery_chg_dev *bcdev,
					u32 version, size_t size)
{
	struct wireless_fw_check_req req_msg = {};

	bcdev->wls_fw_update_reqd = false;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_CHECK_UPDATE;
	req_msg.fw_version = version;
	req_msg.fw_size = size;

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

#define IDT_FW_MAJOR_VER_OFFSET		0x94
#define IDT_FW_MINOR_VER_OFFSET		0x96
static int wireless_fw_update(struct battery_chg_dev *bcdev, bool force)
{
	const struct firmware *fw;
	struct psy_state *pst;
	u32 version;
	u16 maj_ver, min_ver;
	int rc;

	pm_stay_awake(bcdev->dev);

	/*
	 * Check for USB presence. If nothing is connected, check whether
	 * battery SOC is at least 50% before allowing FW update.
	 */
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_ONLINE);
	if (rc < 0)
		goto out;

	if (!pst->prop[USB_ONLINE]) {
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_CAPACITY);
		if (rc < 0)
			goto out;

		if ((pst->prop[BATT_CAPACITY] / 100) < 50) {
			chg_err("Battery SOC should be at least 50%% or connect charger\n");
			rc = -EINVAL;
			goto out;
		}
	}

	rc = firmware_request_nowarn(&fw, bcdev->wls_fw_name, bcdev->dev);
	if (rc) {
		chg_err("Couldn't get firmware rc=%d\n", rc);
		goto out;
	}

	if (!fw || !fw->data || !fw->size) {
		chg_err("Invalid firmware\n");
		rc = -EINVAL;
		goto release_fw;
	}

	if (fw->size < SZ_16K) {
		chg_err("Invalid firmware size %zu\n", fw->size);
		rc = -EINVAL;
		goto release_fw;
	}

	maj_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MAJOR_VER_OFFSET));
	min_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MINOR_VER_OFFSET));
	version = maj_ver << 16 | min_ver;

	if (force)
		version = UINT_MAX;

	pr_debug("FW size: %zu version: %#x\n", fw->size, version);

	rc = wireless_fw_check_for_update(bcdev, version, fw->size);
	if (rc < 0) {
		chg_err("Wireless FW update not needed, rc=%d\n", rc);
		goto release_fw;
	}

	if (!bcdev->wls_fw_update_reqd) {
		pr_warn("Wireless FW update not required\n");
		goto release_fw;
	}

	/* Wait for IDT to be setup by charger firmware */
	msleep(WLS_FW_PREPARE_TIME_MS);

	reinit_completion(&bcdev->fw_update_ack);
	rc = wireless_fw_send_firmware(bcdev, fw);
	if (rc < 0) {
		chg_err("Failed to send FW chunk, rc=%d\n", rc);
		goto release_fw;
	}

	rc = wait_for_completion_timeout(&bcdev->fw_update_ack,
				msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
	if (!rc) {
		chg_err("Error, timed out updating firmware\n");
		rc = -ETIMEDOUT;
		goto release_fw;
	} else {
		rc = 0;
	}

	chg_info("Wireless FW update done\n");

release_fw:
	release_firmware(fw);
out:
	pm_relax(bcdev->dev);

	return rc;
}

static ssize_t wireless_fw_version_show(struct class *c,
					struct class_attribute *attr,
					char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct wireless_fw_get_version_req req_msg = {};
	int rc;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_GET_VERSION;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		chg_err("Failed to get FW version rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%#x\n", bcdev->wls_fw_version);
}
static CLASS_ATTR_RO(wireless_fw_version);

static ssize_t wireless_fw_force_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, true);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_force_update);

static ssize_t wireless_fw_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, false);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_update);

static ssize_t usb_typec_compliant_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_TYPEC_COMPLIANT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			(int)pst->prop[USB_TYPEC_COMPLIANT]);
}
static CLASS_ATTR_RO(usb_typec_compliant);

static ssize_t usb_real_type_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_REAL_TYPE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			get_usb_type_name(pst->prop[USB_REAL_TYPE]));
}
static CLASS_ATTR_RO(usb_real_type);

static ssize_t restrict_cur_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (kstrtou32(buf, 0, &fcc_ua) || fcc_ua > bcdev->thermal_fcc_ua)
		return -EINVAL;

	prev_fcc_ua = bcdev->restrict_fcc_ua;
	bcdev->restrict_fcc_ua = fcc_ua;
	if (bcdev->restrict_chg_en) {
		rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
		if (rc < 0) {
			bcdev->restrict_fcc_ua = prev_fcc_ua;
			return rc;
		}
	}

	return count;
}

static ssize_t restrict_cur_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%u\n", bcdev->restrict_fcc_ua);
}
static CLASS_ATTR_RW(restrict_cur);

static ssize_t restrict_chg_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	bcdev->restrict_chg_en = val;
	rc = __battery_psy_set_charge_current(bcdev, bcdev->restrict_chg_en ?
			bcdev->restrict_fcc_ua : bcdev->thermal_fcc_ua);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t restrict_chg_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->restrict_chg_en);
}
static CLASS_ATTR_RW(restrict_chg);

static ssize_t fake_soc_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	bcdev->fake_soc = val;
	pr_debug("Set fake soc to %d\n", val);

	if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) && pst->psy)
		power_supply_changed(pst->psy);

	return count;
}

static ssize_t fake_soc_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->fake_soc);
}
static CLASS_ATTR_RW(fake_soc);

static ssize_t wireless_boost_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_WLS],
				WLS_BOOST_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t wireless_boost_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = read_property_id(bcdev, pst, WLS_BOOST_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[WLS_BOOST_EN]);
}
static CLASS_ATTR_RW(wireless_boost_en);

static ssize_t moisture_detection_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_USB],
				USB_MOISTURE_DET_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t moisture_detection_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_EN]);
}
static CLASS_ATTR_RW(moisture_detection_en);

static ssize_t moisture_detection_status_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_STS);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_STS]);
}
static CLASS_ATTR_RO(moisture_detection_status);

static ssize_t resistance_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[BATT_RESISTANCE]);
}
static CLASS_ATTR_RO(resistance);

static ssize_t soh_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SOH]);
}
static CLASS_ATTR_RO(soh);

static ssize_t ship_mode_en_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtobool(buf, &bcdev->ship_mode_en))
		return -EINVAL;

	return count;
}

static ssize_t ship_mode_en_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->ship_mode_en);
}
static CLASS_ATTR_RW(ship_mode_en);

static struct attribute *battery_class_attrs[] = {
	&class_attr_soh.attr,
	&class_attr_resistance.attr,
	&class_attr_moisture_detection_status.attr,
	&class_attr_moisture_detection_en.attr,
	&class_attr_wireless_boost_en.attr,
	&class_attr_fake_soc.attr,
	&class_attr_wireless_fw_update.attr,
	&class_attr_wireless_fw_force_update.attr,
	&class_attr_wireless_fw_version.attr,
	&class_attr_ship_mode_en.attr,
	&class_attr_restrict_chg.attr,
	&class_attr_restrict_cur.attr,
	&class_attr_usb_real_type.attr,
	&class_attr_usb_typec_compliant.attr,
	NULL,
};
ATTRIBUTE_GROUPS(battery_class);

#ifdef CONFIG_DEBUG_FS
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev)
{
	int rc;
	struct dentry *dir, *file;

	dir = debugfs_create_dir("battery_charger", NULL);
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		chg_err("Failed to create charger debugfs directory, rc=%d\n",
			rc);
		return;
	}

	file = debugfs_create_bool("block_tx", 0600, dir, &bcdev->block_tx);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		chg_err("Failed to create block_tx debugfs file, rc=%d\n",
			rc);
		goto error;
	}

	bcdev->debugfs_dir = dir;

	return;
error:
	debugfs_remove_recursive(dir);
}
#else
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev) { }
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC

static bool oplus_vchg_trig_is_support(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return false;
	}
	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0)
		return false;

	if (get_PCB_Version() >= EVT1)
		return true;
	return false;
}

static int oplus_vchg_trig_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		pr_debug(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.vchg_trig_pinctrl = devm_pinctrl_get(bcdev->dev);

	bcdev->oplus_custom_gpio.vchg_trig_default =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.vchg_trig_pinctrl, "vchg_trig_default");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_default)) {
		chg_err("get vchg_trig_default\n");
		return -EINVAL;
	}

	if (bcdev->oplus_custom_gpio.vchg_trig_gpio > 0) {
		gpio_direction_input(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	}
	pinctrl_select_state(bcdev->oplus_custom_gpio.vchg_trig_pinctrl,
		bcdev->oplus_custom_gpio.vchg_trig_default);

	chg_err("get vchg_trig_default level[%d]\n", gpio_get_value(bcdev->oplus_custom_gpio.vchg_trig_gpio));
	return 0;
}

static int oplus_get_vchg_trig_gpio_val(void)
{
	int level = 1;
	static int pre_level = 1;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("chip is NULL!\n");
		return -1;
	}

	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0) {
		chg_err("vchg_trig_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_pinctrl)
			|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	level = gpio_get_value(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	if (pre_level ^ level) {
		pre_level = level;
		chg_err("!!!!! vchg_trig gpio level[%d], wired[%d]\n", level, !level);
	}
	return level;
}

static int vchg_trig_status = -1;
static int oplus_get_vchg_trig_status(void)
{
	if (vchg_trig_status == -1) {
		vchg_trig_status = !!oplus_get_vchg_trig_gpio_val();
	}
	return vchg_trig_status;
}

static void oplus_vchg_trig_work(struct work_struct *work)
{
#ifdef OPLUS_CHG_UNDEF /* TODO */
	int level;
	static bool pre_otg = false;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct oplus_chg_chip *chip = get_oplus_chg_chip();

	if (!chip || !bcdev) {
		chg_err("chip or bcdev is NULL!\n");
		return;
	}

	level = oplus_get_vchg_trig_gpio_val();
	vchg_trig_status = !!level;
	if (level == 0) {
		if (bcdev->otg_online == true) {
			pre_otg = true;
			return;
		}
		if (chip->wireless_support)
			oplus_switch_to_wired_charge(bcdev);
	} else {
		if (pre_otg == true) {
			pre_otg = false;
			return;
		}
		if (chip->wireless_support
			&& chip->voocphy.fastchg_to_warm == false
			&& chip->voocphy.fastchg_to_normal == false)
			oplus_switch_from_wired_charge(bcdev);
	}

	if (chip->voocphy.fastchg_to_warm == false
		&& chip->voocphy.fastchg_to_normal == false) {
		oplus_chg_wake_update_work();
	}
#endif
}

static void oplus_vchg_trig_irq_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		pr_debug(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return;
	}

	bcdev->vchg_trig_irq = gpio_to_irq(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	pr_debug(KERN_ERR "[OPLUS_CHG][%s]: vchg_trig_irq[%d]!\n", __func__, bcdev->vchg_trig_irq);
}

#define VCHG_TRIG_DELAY_MS	50
irqreturn_t oplus_vchg_trig_change_handler(int irq, void *data)
{
	struct battery_chg_dev *bcdev = data;

	cancel_delayed_work_sync(&bcdev->vchg_trig_work);
	pr_debug(KERN_ERR "[OPLUS_CHG][%s]: scheduling vchg_trig work!\n", __func__);
	schedule_delayed_work(&bcdev->vchg_trig_work, msecs_to_jiffies(VCHG_TRIG_DELAY_MS));

	return IRQ_HANDLED;
}

static void oplus_vchg_trig_irq_register(struct battery_chg_dev *bcdev)
{
	int ret = 0;

	if (!bcdev) {
		pr_debug(KERN_ERR "[OPLUS_CHG][%s]: bcdev not ready!\n", __func__);
		return;
	}

	ret = devm_request_threaded_irq(bcdev->dev, bcdev->vchg_trig_irq,
			NULL, oplus_vchg_trig_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "vchg_trig_change", bcdev);
	if (ret < 0) {
		chg_err("Unable to request vchg_trig_change irq: %d\n", ret);
	}
	pr_debug(KERN_ERR "%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(bcdev->vchg_trig_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: vchg_trig_irq failed %d\n", ret);
	}
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
static void smbchg_enter_shipmode_pmic(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = NULL;

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = write_property_id(bcdev, pst, BATT_SET_SHIP_MODE, 1);
	if (rc) {
		chg_err("set ship mode fail, rc=%d\n", rc);
		return;
	}
	chg_debug("power off after 15s\n");
}

static int oplus_chg_parse_custom_dt(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	node = bcdev->dev->of_node;

	bcdev->oplus_custom_gpio.vchg_trig_gpio =
		of_get_named_gpio(node, "qcom,vchg_trig-gpio", 0);
	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0) {
		chg_err("Couldn't read qcom,vchg_trig-gpio rc = %d, vchg_trig-gpio:%d\n",
					rc, bcdev->oplus_custom_gpio.vchg_trig_gpio);
	} else {
		if (oplus_vchg_trig_is_support() == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.vchg_trig_gpio, "vchg_trig-gpio");
			if (rc) {
				chg_err("unable to vchg_trig-gpio:%d\n",
							bcdev->oplus_custom_gpio.vchg_trig_gpio);
			} else {
				rc = oplus_vchg_trig_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init vchg_trig-gpio:%d\n",
							bcdev->oplus_custom_gpio.vchg_trig_gpio);
				else
					oplus_vchg_trig_irq_init(bcdev);
			}
		}
		chg_err("vchg_trig-gpio:%d\n", bcdev->oplus_custom_gpio.vchg_trig_gpio);
	}

	return 0;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static int battery_chg_parse_dt(struct battery_chg_dev *bcdev)
{
	struct device_node *node = bcdev->dev->of_node;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int i, rc, len;
	u32 prev, val;

#ifdef OPLUS_FEATURE_CHG_BASIC
	bcdev->otg_online = false;
	bcdev->pd_svooc = false;
#endif
	of_property_read_string(node, "qcom,wireless-fw-name",
				&bcdev->wls_fw_name);
	bcdev->oem_lcm_check = of_property_read_bool(node, "oplus,oem-lcm-check");
	bcdev->voocphy_support = of_property_read_bool(node, "oplus,voocphy_support");

	rc = of_property_count_elems_of_size(node, "qcom,thermal-mitigation",
						sizeof(u32));
	if (rc <= 0)
		return 0;

	len = rc;

	rc = read_property_id(bcdev, pst, BATT_CHG_CTRL_LIM_MAX);
	if (rc < 0)
		return rc;

	prev = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	for (i = 0; i < len; i++) {
		rc = of_property_read_u32_index(node, "qcom,thermal-mitigation",
						i, &val);
		if (rc < 0)
			return rc;

		if (val > prev) {
			chg_err("Thermal levels should be in descending order\n");
			bcdev->num_thermal_levels = -EINVAL;
			return 0;
		}

		prev = val;
	}

	bcdev->thermal_levels = devm_kcalloc(bcdev->dev, len + 1,
					sizeof(*bcdev->thermal_levels),
					GFP_KERNEL);
	if (!bcdev->thermal_levels)
		return -ENOMEM;

	/*
	 * Element 0 is for normal charging current. Elements from index 1
	 * onwards is for thermal mitigation charging currents.
	 */

	bcdev->thermal_levels[0] = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	rc = of_property_read_u32_array(node, "qcom,thermal-mitigation",
					&bcdev->thermal_levels[1], len);
	if (rc < 0) {
		chg_err("Error in reading qcom,thermal-mitigation, rc=%d\n", rc);
		return rc;
	}

	bcdev->num_thermal_levels = len;
	bcdev->thermal_fcc_ua = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	return 0;
}

static int battery_chg_ship_mode(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct battery_charger_ship_mode_req_msg msg = { { 0 } };
	struct battery_chg_dev *bcdev = container_of(nb, struct battery_chg_dev,
						     reboot_notifier);
	int rc;

	if (!bcdev->ship_mode_en)
		return NOTIFY_DONE;

	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_SHIP_MODE_REQ_SET;
	msg.ship_mode_type = SHIP_MODE_PMIC;

	if (code == SYS_POWER_OFF) {
		rc = battery_chg_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			pr_emerg("Failed to write ship mode: %d\n", rc);
	}

	return NOTIFY_DONE;
}

/**********************************************************************
 * battery charge ops *
 **********************************************************************/
#ifdef OPLUS_FEATURE_CHG_BASIC

#ifdef OPLUS_CHG_UNDEF
int oplus_get_voocphy_enable()
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return 0;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VOOCPHY_ENABLE);
	if (rc) {
		chg_err("get enable voocphy fail, rc=%d\n", rc);
		return 0;
	} else {
		chg_err("get enable voocphy success, rc=%d\n", rc);
	}

	return pst->prop[USB_VOOCPHY_ENABLE];
}

int oplus_voocphy_enable(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_VOOCPHY_ENABLE, enable);
	if (rc) {
		chg_err("set enable voocphy fail, rc=%d\n", rc);
	} else {
		chg_err("set enable voocphy success, rc=%d\n", rc);
	}

	return rc;
}

static void oplus_voocphy_err_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = g_bcdev;
	struct oplus_chg_chip *chip = get_oplus_chg_chip();

	if (!chip || !bcdev) {
		pr_debug(KERN_ERR "!!!chip or bcdev null, oplus_voocphy_err_work\n");
		return;
	}

	chg_err("!!!call\n");
	if (chip->voocphy.fastchg_ing == false && bcdev->voocphy_err_check) {
		chg_err("!!!happend\n");
		bcdev->voocphy_err_check = false;
		oplus_chg_suspend_charger();
		usleep_range(1000000, 1000010);
		if (chip->mmi_chg) {
			oplus_chg_unsuspend_charger();
			chip->chg_ops->charging_enable();
			oplus_voocphy_reset_again();
		}
	}
}

int oplus_voocphy_reset_again(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_VOOCPHY_RESET_AGAIN, true);
	if (rc) {
		chg_err("set voocphy_reset_again fail, rc=%d\n", rc);
	} else {
		chg_err("set voocphy_reset_again success, rc=%d\n", rc);
	}

	return rc;
}
#endif

__maybe_unused static int oplus_otg_ap_enable(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_AP_ENABLE, enable);
	if (rc) {
		chg_err("oplus_otg_ap_enable fail, rc=%d\n", rc);
	} else {
		chg_err("oplus_otg_ap_enable, rc=%d\n", rc);
	}

	return rc;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

/**********************************************************************
 * battery gauge ops *
 **********************************************************************/
#ifdef OPLUS_FEATURE_CHG_BASIC
__maybe_unused static int fg_sm8350_get_battery_mvolts(void)
{
	int rc = 0;
	int prop_id = 0;
	static int volt = 4000;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->voocphy_support) {
		volt = DIV_ROUND_CLOSEST(bcdev->read_buffer_dump.data_buffer[2], 1000);
		return volt;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery volt fail, rc=%d\n", rc);
		return volt;
	}
	volt = DIV_ROUND_CLOSEST(pst->prop[prop_id], 1000);

	return volt;
}

static int fg_sm8350_get_battery_temperature(void)
{
	int rc = 0;
	int prop_id = 0;
	static int temp = 250;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->voocphy_support) {
		temp = bcdev->read_buffer_dump.data_buffer[0];
		goto HIGH_TEMP;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_TEMP);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery temp fail, rc=%d\n", rc);
		return temp;
	}
	temp = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);

HIGH_TEMP:
	if (get_eng_version() == HIGH_TEMP_AGING) {
		pr_debug(KERN_ERR "[OPLUS_CHG]CONFIG_HIGH_TEMP_VERSION enable here, \
				disable high tbat shutdown \n");
		if (temp > 690)
			temp = 690;
	}

	return temp;
}

static int fg_sm8350_get_batt_remaining_capacity(void)
{
	int rc = 0;
	static int batt_rm = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return batt_rm;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->voocphy_support) {
		batt_rm = bcdev->read_buffer_dump.data_buffer[4];
		return batt_rm;
	}

	rc = read_property_id(bcdev, pst, BATT_CHG_COUNTER);
	if (rc < 0) {
		chg_err("read battery chg counter fail, rc=%d\n", rc);
		return batt_rm;
	}
	batt_rm = DIV_ROUND_CLOSEST(pst->prop[BATT_CHG_COUNTER], 1000);

	return batt_rm;
}

static int fg_sm8350_get_battery_soc(void)
{
	int rc = 0;
	int prop_id = 0;
	static int soc = 50;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->voocphy_support) {
		soc = DIV_ROUND_CLOSEST(bcdev->read_buffer_dump.data_buffer[3], 100);
		return soc;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CAPACITY);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery soc fail, rc=%d\n", rc);
		return soc;
	}
	soc = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);

	return soc;
}

static int fg_sm8350_get_average_current(void)
{
	int rc = 0;
	int prop_id = 0;
	static int curr = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->voocphy_support) {
		curr = DIV_ROUND_CLOSEST((int)bcdev->read_buffer_dump.data_buffer[1], 1000);
		return curr;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return curr;
	}
	curr = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return curr;
}

static int fg_sm8350_get_battery_fcc(void)
{
	static int fcc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		return -1;
	}

	if (bcdev->voocphy_support) {
		fcc = bcdev->read_buffer_dump.data_buffer[6];
		return fcc;
	}

	return fcc;
}

static int fg_sm8350_get_battery_cc(void)
{
	static int cc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		return -1;
	}

	if (bcdev->voocphy_support) {
		cc = bcdev->read_buffer_dump.data_buffer[7];
		return cc;
	}

	return cc;
}

static int fg_sm8350_get_battery_soh(void)
{
	static int soh = 0;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		return -1;
	}

	if (bcdev->voocphy_support) {
		soh = bcdev->read_buffer_dump.data_buffer[8];
		return soh;
	}

	return soh;
}

static bool fg_sm8350_get_battery_authenticate(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return false;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_BATTERY_AUTH);
	if (rc < 0) {
		chg_err("read battery auth fail, rc=%d\n", rc);
		return false;
	}
	chg_err("read battery auth success, auth=%d\n", pst->prop[BATT_BATTERY_AUTH]);

	return pst->prop[BATT_BATTERY_AUTH];
}

static void fg_sm8350_set_battery_full(bool full)
{
	/*Do nothing*/
}
/*
static int fg_sm8350_get_prev_battery_mvolts(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->batt_volt;
}

static int fg_sm8350_get_prev_battery_temperature(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->temperature;
}

static int fg_sm8350_get_prev_battery_soc(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->soc;
}

static int fg_sm8350_get_prev_average_current(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->icharging;
}

static int fg_sm8350_get_prev_batt_remaining_capacity(void)
{
	return 0;
}
*/
static int fg_sm8350_get_battery_mvolts_2cell_max(void)
{
	return fg_sm8350_get_battery_mvolts();
}

static int fg_sm8350_get_battery_mvolts_2cell_min(void)
{
	return fg_sm8350_get_battery_mvolts();
}
/*
static int fg_sm8350_get_prev_battery_mvolts_2cell_max(void)
{
	return 4000;
}

static int fg_sm8350_get_prev_battery_mvolts_2cell_min(void)
{
	return 4000;
}
*/
static int fg_bq28z610_modify_dod0(void)
{
	return 0;
}

static int fg_bq28z610_update_soc_smooth_parameter(void)
{
	return 0;
}

static int fg_bq28z610_get_battery_balancing_status(void)
{
	return 0;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
static ssize_t proc_debug_reg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	int rc = 0;
	int reg_data = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return 0;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_DEBUG_REG);
	if (rc) {
		chg_err("get enable voocphy fail, rc=%d\n", rc);
	} else {
		chg_err("get enable voocphy success, rc=%d\n", rc);
	}

	reg_data = pst->prop[USB_DEBUG_REG];

	sprintf(page, "0x%x\n", reg_data);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_debug_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
	int rc = 0;
	char buffer[10] = {0};
	int add_data = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (count > 10) {
		chg_err("%s: count so len.\n", __func__);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: read proc input error.\n", __func__);
		return -EFAULT;
	}

	if (1 != sscanf(buffer, "0x%x", &add_data)) {
		chg_err("invalid content: '%s', length = %zd\n", buf, count);
		return -EFAULT;
	}
	chg_err("%s: add:0x%x, data:0x%x\n", __func__, (add_data >> 8) & 0xffff, (add_data & 0xff));

	rc = write_property_id(bcdev, pst, USB_DEBUG_REG, add_data);
	if (rc) {
		chg_err("set usb_debug_reg fail, rc=%d\n", rc);
	} else {
		chg_err("set usb_debug_reg success, rc=%d\n", rc);
	}

	return count;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_debug_reg_ops =
{
	.read = proc_debug_reg_read,
	.write  = proc_debug_reg_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_debug_reg_ops =
{
	.proc_write  = proc_debug_reg_write,
	.proc_read  = proc_debug_reg_read,
};
#endif

static int init_debug_reg_proc(struct battery_chg_dev *bcdev)
{
	int ret = 0;
	struct proc_dir_entry *pr_entry_da = NULL;
	struct proc_dir_entry *pr_entry_tmp = NULL;

	pr_entry_da = proc_mkdir("8350_reg", NULL);
	if (pr_entry_da == NULL) {
		ret = -ENOMEM;
		chg_debug("%s: Couldn't create debug_reg proc entry\n", __func__);
	}

	pr_entry_tmp = proc_create_data("reg", 0644, pr_entry_da, &proc_debug_reg_ops, bcdev);
	if (pr_entry_tmp == NULL) {
		ret = -ENOMEM;
		chg_debug("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	return 0;
}

static int battery_chg_pm_resume(struct device *dev)
{
	struct battery_chg_dev *bcdev = dev_get_drvdata(dev);
	atomic_set(&bcdev->suspended, 0);
	oplus_chg_ic_virq_trigger(bcdev->gauge_ic, OPLUS_IC_VIRQ_RESUME);
	return 0;
}

static int battery_chg_pm_suspend(struct device *dev)
{
	struct battery_chg_dev *bcdev = dev_get_drvdata(dev);
	atomic_set(&bcdev->suspended, 1);
	return 0;
}

static const struct dev_pm_ops battery_chg_pm_ops = {
	.resume		= battery_chg_pm_resume,
	.suspend	= battery_chg_pm_suspend,
};
#endif

static int oplus_chg_8350_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;
	return 0;
}

static int oplus_chg_8350_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	return 0;
}

static int oplus_chg_8350_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int  oplus_chg_8350_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int oplus_chg_8350_input_present(struct oplus_chg_ic_dev *ic_dev, bool *present)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	bool vbus_rising = false;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_ONLINE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_rising fail, rc=%d\n", rc);
		return rc;
	}
	vbus_rising = pst->prop[prop_id];

	chg_err("vbus_rising=%d\n", vbus_rising);

	if (vbus_rising == false && pst->prop[prop_id] == 2) {
		chg_err("USBIN low but svooc/vooc started\n");
		vbus_rising = true;
	}

	*present = vbus_rising;

	return rc;
}

static int oplus_chg_8350_input_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, suspend ? 0 : 0xFFFFFFFF);
	if (rc)
		chg_err("set %s fail, rc=%d\n",
			suspend ? "suspend" : "unsuspend", rc);

	return rc;
}

static int oplus_chg_8350_input_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	return 0;
}

static int oplus_chg_8350_output_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, suspend ? 0 : 1);
	if (rc)
		chg_err("set %s charging fail, rc=%d\n", suspend ? "disable" : "enable", rc);

	return rc;
}

static int oplus_chg_8350_output_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_CHG_EN);
	if (rc) {
		chg_err("get battery charging status fail, rc=%d\n", rc);
		return rc;
	}

	*suspend = !!pst->prop[BATT_CHG_EN];

	return rc;
}

static int oplus_chg_8350_set_icl(struct oplus_chg_ic_dev *ic_dev,
				  bool vooc_mode, bool step, int icl_ma)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, icl_ma * 1000);
	if (rc)
		chg_err("set icl to %d mA fail, rc=%d\n", icl_ma, rc);

	return rc;
}

static int oplus_chg_8350_get_icl(struct oplus_chg_ic_dev *ic_dev, int *icl_ma)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb icl fail, rc=%d\n", rc);
		return rc;
	}
	*icl_ma = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return 0;
}

static int oplus_chg_8350_set_fcc(struct oplus_chg_ic_dev *ic_dev, int fcc_ma)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, fcc_ma * 1000);
	if (rc)
		chg_err("set fcc to %d mA fail, rc=%d\n", fcc_ma, rc);

	return rc;
}

static int oplus_chg_8350_set_fv(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_MAX);
	rc = write_property_id(bcdev, pst, prop_id, fv_mv);
	if (rc)
		chg_err("set fv to %d mV fail, rc=%d\n", fv_mv, rc);

	return rc;
}

static int oplus_chg_8350_set_iterm(struct oplus_chg_ic_dev *ic_dev, int iterm_ma)
{
	return 0;
}

static int oplus_chg_8350_set_rechg_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	return 0;
}

static int oplus_chg_8350_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr_ma)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read ibus fail, rc=%d\n", rc);
		return rc;
	}
	*curr_ma = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return rc;
}

static int oplus_chg_8350_get_input_vol(struct oplus_chg_ic_dev *ic_dev, int *vol_mv)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_volt fail, rc=%d\n", rc);
		return rc;
	}
	*vol_mv = pst->prop[prop_id] / 1000;
	*vol_mv += 300;

	return rc;
}

static int oplus_chg_8350_otg_boost_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = write_property_id(bcdev, pst, USB_OTG_VBUS_REGULATOR_ENABLE, en ? 1 : 0);
	if (rc) {
		chg_err("%s otg boost fail, rc=%d\n", en ? "enable" : "disable", rc);
		return rc;
	}
	/* schedule_delayed_work(&bcdev->otg_status_check_work, 0); */

	return rc;
}

static int oplus_chg_8350_set_otg_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	return 0;
}

static int oplus_chg_8350_set_otg_boost_curr_limit(struct oplus_chg_ic_dev *ic_dev, int curr_ma)
{
	return 0;
}

static int oplus_chg_8350_aicl_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	/* TODO */

	return rc;
}

static int oplus_chg_8350_aicl_rerun(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	/* TODO */

	return rc;
}

static int oplus_chg_8350_aicl_reset(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	/* TODO */

	return rc;
}

static int oplus_chg_8350_get_cc_orientation(struct oplus_chg_ic_dev *ic_dev, int *orientation)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_TYPEC_CC_ORIENTATION);
	if (rc < 0) {
		pr_debug(KERN_ERR "!!![OPLUS_CHG] read typec_cc_orientation fail\n");
		return rc;
	}
	*orientation = pst->prop[USB_TYPEC_CC_ORIENTATION];

	return rc;
}

static int oplus_chg_8350_get_hw_detect(struct oplus_chg_ic_dev *ic_dev, int *detected)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_CID_STATUS);
	if (rc < 0) {
		chg_err("read cid_status fail, rc=%d\n", rc);
		return rc;
	}
	*detected = pst->prop[USB_CID_STATUS];

	return 0;
}

static int oplus_chg_8350_get_charger_type(struct oplus_chg_ic_dev *ic_dev, int *type)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb charger_type fail, rc=%d\n", rc);
		return rc;
	}
	switch (pst->prop[prop_id]) {
	case POWER_SUPPLY_USB_TYPE_UNKNOWN:
		*type = OPLUS_CHG_USB_TYPE_UNKNOWN;
		break;
	case POWER_SUPPLY_USB_TYPE_SDP:
		*type = OPLUS_CHG_USB_TYPE_SDP;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		*type = OPLUS_CHG_USB_TYPE_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		*type = OPLUS_CHG_USB_TYPE_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		*type = OPLUS_CHG_USB_TYPE_ACA;
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		*type = OPLUS_CHG_USB_TYPE_C;
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
		*type = OPLUS_CHG_USB_TYPE_PD;
		break;
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
		*type = OPLUS_CHG_USB_TYPE_PD_DRP;
		break;
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		*type = OPLUS_CHG_USB_TYPE_PD_PPS;
		break;
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
		*type = OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID;
		break;
	}

	if (*type != POWER_SUPPLY_USB_TYPE_PD &&
	    *type != POWER_SUPPLY_USB_TYPE_PD_DRP &&
	    *type != POWER_SUPPLY_USB_TYPE_PD_PPS) {
		rc = read_property_id(bcdev, pst, USB_ADAP_SUBTYPE);
		if (rc < 0) {
			chg_err("read charger subtype fail, rc=%d\n", rc);
			rc = 0;
		}
		switch (pst->prop[USB_ADAP_SUBTYPE]) {
		case CHARGER_SUBTYPE_FASTCHG_VOOC:
			*type = OPLUS_CHG_USB_TYPE_VOOC;
			break;
		case CHARGER_SUBTYPE_FASTCHG_SVOOC:
			*type = OPLUS_CHG_USB_TYPE_SVOOC;
			break;
		case CHARGER_SUBTYPE_QC:
			*type = OPLUS_CHG_USB_TYPE_QC2;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int oplus_chg_8350_rerun_bc12(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int oplus_chg_8350_qc_detect_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->hvdcp_disable == true) {
		chg_err("hvdcp_disable!\n");
		return -EINVAL;
	}

	if (en)
		rc = write_property_id(bcdev, pst, BATT_SET_QC, 0);

	return rc;
}

#define PWM_COUNT	5
static int oplus_chg_8350_shipmode_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	smbchg_enter_shipmode_pmic(bcdev);

	return 0;
}

static int oplus_chg_8350_set_qc_config(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_qc_version version, int vol_mv)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	switch (version) {
	case OPLUS_CHG_QC_2_0:
		if (vol_mv != 5000 && vol_mv != 9000) {
			chg_err("Unsupported qc voltage(=%d)\n", vol_mv);
			return -EINVAL;
		}
		rc = write_property_id(bcdev, pst, BATT_SET_QC, vol_mv);
		if (rc)
			chg_err("set QC to %d mV fail, rc=%d\n", vol_mv, rc);
		else
			chg_err("set QC to %d mV, rc=%d\n", vol_mv, rc);
		break;
	case OPLUS_CHG_QC_3_0:
	default:
		chg_err("Unsupported qc version(=%d)\n", version);
		return -EINVAL;
	}

	return rc;
}

static int oplus_chg_8350_set_pd_config(struct oplus_chg_ic_dev *ic_dev, u32 pdo)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int vol_mv;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	switch (PD_SRC_PDO_TYPE(pdo)) {
	case PD_SRC_PDO_TYPE_FIXED:
		vol_mv = PD_SRC_PDO_FIXED_VOLTAGE(pdo) * 50;
		if (vol_mv != 5000 && vol_mv != 9000) {
			chg_err("Unsupported pd voltage(=%d)\n", vol_mv);
			return -EINVAL;
		}
		rc = write_property_id(bcdev, pst, BATT_SET_PDO, vol_mv);
		if (rc)
			chg_err("set PD to %d mV fail, rc=%d\n", vol_mv, rc);
		else
			chg_err("set PD to %d mV, rc=%d\n", vol_mv, rc);
		break;
	case PD_SRC_PDO_TYPE_BATTERY:
	case PD_SRC_PDO_TYPE_VARIABLE:
	case PD_SRC_PDO_TYPE_AUGMENTED:
	default:
		chg_err("Unsupported pdo type(=%d)\n", PD_SRC_PDO_TYPE(pdo));
		return -EINVAL;
	}

	return rc;
}

static int oplus_chg_8350_get_props_from_adsp_by_buffer(struct oplus_chg_ic_dev *ic_dev)
{
	oplus_get_props_from_adsp_by_buffer();
	return 0;
}

static int oplus_chg_8350_gauge_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	struct battery_chg_dev *bcdev;

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	*suspend = atomic_read(&bcdev->suspended);

	return 0;
}

static int oplus_chg_8350_voocphy_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	/* return oplus_voocphy_enable(en); */
	return 0;
}

static int oplus_chg_8350_voocphy_reset_again(struct oplus_chg_ic_dev *ic_dev)
{
	/* return oplus_voocphy_reset_again(); */
	return 0;
}

static int oplus_chg_8350_get_charger_cycle(struct oplus_chg_ic_dev *ic_dev, int *cycle)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->voocphy_support) {
		*cycle = bcdev->read_buffer_dump.data_buffer[5];
		return 0;
	}

	rc = read_property_id(bcdev, pst, BATT_CYCLE_COUNT);
	if (rc) {
		chg_err("get charger_cycle fail, rc=%d\n", rc);
		return rc;
	}

	chg_err("get charger_cycle[%d]\n", pst->prop[BATT_CYCLE_COUNT]);
	*cycle = pst->prop[BATT_CYCLE_COUNT];

	return rc;
}

static int oplus_chg_8350_wls_boost_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_WLS];

	rc = write_property_id(bcdev, pst, WLS_BOOST_EN, en ? 1 : 0);
	if (rc)
		chg_err("%s wls boost fail, rc=%d\n", en ? "enable" : "disable", rc);

	return rc;
}

static int oplus_chg_8350_set_wls_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	return 0;
}

static int oplus_chg_8350_set_wls_boost_curr_limit(struct oplus_chg_ic_dev *ic_dev, int curr_ma)
{
	return 0;
}

static int oplus_chg_8350_get_shutdown_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_RTC_SOC);
	if (rc < 0) {
		chg_err("read battery rtc soc fail, rc=%d\n", rc);
		return rc;
	}
	*soc = pst->prop[BATT_RTC_SOC];
	chg_info("read battery rtc soc success, rtc_soc=%d\n", pst->prop[BATT_RTC_SOC]);

	return rc;
}

static int oplus_chg_8350_backup_soc(struct oplus_chg_ic_dev *ic_dev, int soc)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_RTC_SOC, soc);
	if (rc) {
		chg_err("set battery rtc soc fail, rc=%d\n", rc);
		return 0;
	}
	chg_info("write battery rtc soc success, rtc_soc=%d\n", soc);

	return rc;
}

static int oplus_chg_8350_get_vbus_collapse_status(struct oplus_chg_ic_dev *ic_dev, bool *collapse)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VBUS_COLLAPSE_STATUS);
	if (rc < 0) {
		chg_err("read usb vbus_collapse_status fail, rc=%d\n", rc);
		return rc;
	}
	*collapse = pst->prop[USB_VBUS_COLLAPSE_STATUS];
	chg_info("read usb vbus_collapse_status[%d]\n", *collapse);
	return rc;
}

static int oplus_chg_8350_get_typec_mode(struct oplus_chg_ic_dev *ic_dev,
					enum oplus_chg_typec_port_role_type *mode)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		chg_err("read typec mode fail, rc=%d\n", rc);
		return rc;
	}
	*mode = pst->prop[USB_TYPEC_MODE];

	chg_info("typec mode=%d\n", *mode);
	return rc;
}

static int oplus_chg_8350_set_typec_mode(struct oplus_chg_ic_dev *ic_dev,
					enum oplus_chg_typec_port_role_type mode)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, mode);
	if (rc < 0)
		chg_err("set typec mode(=%d) error\n", mode);

	return rc;
}

static int oplus_chg_8350_set_otg_switch_status(struct oplus_chg_ic_dev *ic_dev,
						bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_SWITCH, en);
	if (rc < 0)
		chg_err("%s otg switch error, rc=%d\n", en ? "enable" : "disable", rc);

	return rc;
}

static int oplus_chg_8350_get_otg_switch_status(struct oplus_chg_ic_dev *ic_dev,
						bool *en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_OTG_SWITCH);
	if (rc < 0) {
		chg_err("get otg switch status error, rc=%d\n", rc);
		return rc;
	}
	*en = !!pst->prop[USB_OTG_SWITCH];

	return rc;
}

static int oplus_chg_8350_cc_detect_happened(struct oplus_chg_ic_dev *ic_dev)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_CCDETECT_HAPPENED, 1);
	if (rc < 0)
		chg_err("write ccdetect plugout fail, rc=%d\n", rc);
	else
		chg_info("cc detect plugout, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_8350_set_curr_level(struct oplus_chg_ic_dev *ic_dev, int cool_down)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_COOL_DOWN, cool_down);
	if (rc < 0)
		chg_err("write cool down fail, rc=%d\n", rc);
	else
		chg_info("set cool down to %d, rc=%d\n", cool_down, rc);

	return rc;
}

static int oplus_chg_8350_set_match_temp(struct oplus_chg_ic_dev *ic_dev, int match_temp)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_MATCH_TEMP, match_temp);
	if (rc < 0)
		chg_err("write match temp fail, rc=%d\n", rc);
	else
		chg_info("set matchtemp to %d, rc=%d\n", match_temp, rc);

	return rc;
}

static int oplus_chg_8350_get_otg_enbale(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*enable = bcdev->otg_online;

	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
static void *oplus_chg_8350_buck_get_func(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, oplus_chg_8350_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, oplus_chg_8350_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP, oplus_chg_8350_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST, oplus_chg_8350_smt_test);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_PRESENT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_PRESENT, oplus_chg_8350_input_present);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND, oplus_chg_8350_input_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND, oplus_chg_8350_input_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND, oplus_chg_8350_output_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND, oplus_chg_8350_output_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ICL, oplus_chg_8350_set_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_ICL, oplus_chg_8350_get_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FCC, oplus_chg_8350_set_fcc);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FV:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FV, oplus_chg_8350_set_fv);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ITERM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ITERM, oplus_chg_8350_set_iterm);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL, oplus_chg_8350_set_rechg_vol);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR, oplus_chg_8350_get_input_curr);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL, oplus_chg_8350_get_input_vol);
		break;
	case OPLUS_IC_FUNC_OTG_BOOST_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_OTG_BOOST_ENABLE, oplus_chg_8350_otg_boost_enable);
		break;
	case OPLUS_IC_FUNC_SET_OTG_BOOST_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_BOOST_VOL, oplus_chg_8350_set_otg_boost_vol);
		break;
	case OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT, oplus_chg_8350_set_otg_boost_curr_limit);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_ENABLE, oplus_chg_8350_aicl_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_RERUN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_RERUN, oplus_chg_8350_aicl_rerun);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_RESET, oplus_chg_8350_aicl_reset);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION, oplus_chg_8350_get_cc_orientation);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_HW_DETECT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_HW_DETECT, oplus_chg_8350_get_hw_detect);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE, oplus_chg_8350_get_charger_type);
		break;
	case OPLUS_IC_FUNC_BUCK_RERUN_BC12:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_RERUN_BC12, oplus_chg_8350_rerun_bc12);
		break;
	case OPLUS_IC_FUNC_BUCK_QC_DETECT_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_QC_DETECT_ENABLE, oplus_chg_8350_qc_detect_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE, oplus_chg_8350_shipmode_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_QC_CONFIG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_QC_CONFIG, oplus_chg_8350_set_qc_config);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG, oplus_chg_8350_set_pd_config);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE, oplus_chg_8350_get_props_from_adsp_by_buffer);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_ENABLE, oplus_chg_8350_voocphy_enable);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_RESET_AGAIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_RESET_AGAIN, oplus_chg_8350_voocphy_reset_again);
		break;
	case OPLUS_IC_FUNC_GET_CHARGER_CYCLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_CHARGER_CYCLE, oplus_chg_8350_get_charger_cycle);
		break;
	case OPLUS_IC_FUNC_WLS_BOOST_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_WLS_BOOST_ENABLE, oplus_chg_8350_wls_boost_enable);
		break;
	case OPLUS_IC_FUNC_SET_WLS_BOOST_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_WLS_BOOST_VOL, oplus_chg_8350_set_wls_boost_vol);
		break;
	case OPLUS_IC_FUNC_SET_WLS_BOOST_CURR_LIMIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_WLS_BOOST_CURR_LIMIT, oplus_chg_8350_set_wls_boost_curr_limit);
		break;
	case OPLUS_IC_FUNC_GET_SHUTDOWN_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_SHUTDOWN_SOC, oplus_chg_8350_get_shutdown_soc);
		break;
	case OPLUS_IC_FUNC_BACKUP_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BACKUP_SOC, oplus_chg_8350_backup_soc);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_VBUS_COLLAPSE_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_VBUS_COLLAPSE_STATUS, oplus_chg_8350_get_vbus_collapse_status);
		break;
	case OPLUS_IC_FUNC_GET_TYPEC_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_TYPEC_MODE, oplus_chg_8350_get_typec_mode);
		break;
	case OPLUS_IC_FUNC_SET_TYPEC_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_TYPEC_MODE, oplus_chg_8350_set_typec_mode);
		break;
	case OPLUS_IC_FUNC_SET_OTG_SWITCH_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_SWITCH_STATUS, oplus_chg_8350_set_otg_switch_status);
		break;
	case OPLUS_IC_FUNC_GET_OTG_SWITCH_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_OTG_SWITCH_STATUS, oplus_chg_8350_get_otg_switch_status);
		break;
	case OPLUS_IC_FUNC_CC_DETECT_HAPPENED:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CC_DETECT_HAPPENED, oplus_chg_8350_cc_detect_happened);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_CURR_LEVEL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_CURR_LEVEL, oplus_chg_8350_set_curr_level);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_MATCH_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_MATCH_TEMP, oplus_chg_8350_set_match_temp);
		break;
	case OPLUS_IC_FUNC_GET_OTG_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_OTG_ENABLE, oplus_chg_8350_get_otg_enbale);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq oplus_chg_8350_buck_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_CC_DETECT },
	{ .virq_id = OPLUS_IC_VIRQ_PLUGIN },
	{ .virq_id = OPLUS_IC_VIRQ_CC_CHANGED },
	{ .virq_id = OPLUS_IC_VIRQ_SUSPEND_CHECK },
	{ .virq_id = OPLUS_IC_VIRQ_CHG_TYPE_CHANGE },
	{ .virq_id = OPLUS_IC_VIRQ_RESUME },
};

static int oplus_sm8350_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;

	return 0;
}

static int oplus_sm8350_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;

	return 0;
}

static int oplus_sm8350_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int oplus_sm8350_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int oplus_sm8350_get_batt_vol(struct oplus_chg_ic_dev *ic_dev,
				      int index, int *vol_mv)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (index) {
	case 0:
		*vol_mv = fg_sm8350_get_battery_mvolts_2cell_max();
		break;
	case 1:
		*vol_mv = fg_sm8350_get_battery_mvolts_2cell_min();
		break;
	default:
		chg_err("Unknown index(=%d), max is 1\n", index);
		return -EINVAL;
	}

	return 0;
}

static int oplus_sm8350_get_batt_max(struct oplus_chg_ic_dev *ic_dev,
				      int *vol_mv)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*vol_mv = fg_sm8350_get_battery_mvolts_2cell_max();

	return 0;
}

static int oplus_sm8350_get_batt_min(struct oplus_chg_ic_dev *ic_dev,
				      int *vol_mv)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*vol_mv = fg_sm8350_get_battery_mvolts_2cell_min();

	return 0;
}

static int oplus_sm8350_get_batt_curr(struct oplus_chg_ic_dev *ic_dev,
				       int *curr_ma)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*curr_ma = fg_sm8350_get_average_current();

	return 0;
}

static int oplus_sm8350_get_batt_temp(struct oplus_chg_ic_dev *ic_dev,
				       int *temp)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*temp = fg_sm8350_get_battery_temperature();

	return 0;
}

static int oplus_sm8350_get_batt_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*soc = fg_sm8350_get_battery_soc();

	return 0;
}

static int oplus_sm8350_get_batt_fcc(struct oplus_chg_ic_dev *ic_dev, int *fcc)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*fcc = fg_sm8350_get_battery_fcc();

	return 0;
}

static int oplus_sm8350_get_batt_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*cc = fg_sm8350_get_battery_cc();

	return 0;
}

static int oplus_sm8350_get_batt_rm(struct oplus_chg_ic_dev *ic_dev, int *rm)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*rm = fg_sm8350_get_batt_remaining_capacity();

	return 0;
}

static int oplus_sm8350_get_batt_soh(struct oplus_chg_ic_dev *ic_dev, int *soh)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*soh = fg_sm8350_get_battery_soh();

	return 0;
}

static int oplus_sm8350_get_batt_auth(struct oplus_chg_ic_dev *ic_dev,
				       bool *pass)
{
	*pass = fg_sm8350_get_battery_authenticate();

	return 0;
}

static int oplus_sm8350_get_batt_hmac(struct oplus_chg_ic_dev *ic_dev,
				       bool *pass)
{
	*pass = fg_sm8350_get_battery_authenticate();

	return 0;
}

static int oplus_sm8350_set_batt_full(struct oplus_chg_ic_dev *ic_dev,
				       bool full)
{
	fg_sm8350_set_battery_full(full);

	return 0;
}

static int oplus_sm8350_update_dod0(struct oplus_chg_ic_dev *ic_dev)
{
	return fg_bq28z610_modify_dod0();
}

static int
oplus_sm8350_update_soc_smooth_parameter(struct oplus_chg_ic_dev *ic_dev)
{
	return fg_bq28z610_update_soc_smooth_parameter();
}

static int oplus_sm8350_get_cb_status(struct oplus_chg_ic_dev *ic_dev,
				       int *status)
{
	*status = fg_bq28z610_get_battery_balancing_status();

	return 0;
}

static int oplus_sm8350_set_lock(struct oplus_chg_ic_dev *ic_dev, bool lock)
{
	return 0;
}

static int oplus_sm8350_is_locked(struct oplus_chg_ic_dev *ic_dev, bool *locked)
{
	*locked = false;
	return 0;
}

static int oplus_sm8350_get_batt_num(struct oplus_chg_ic_dev *ic_dev, int *num)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*num = chip->batt_num;

	return 0;
}

static int oplus_sm8350_get_device_type(struct oplus_chg_ic_dev *ic_dev,
					 int *type)
{
	*type = 0;

	return 0;
}

static int
oplus_sm8350_get_device_type_for_vooc(struct oplus_chg_ic_dev *ic_dev,
				       int *type)
{
	*type = 0;

	return 0;
}

static void *oplus_chg_8350_gauge_get_func(struct oplus_chg_ic_dev *ic_dev,
					   enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_sm8350_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_sm8350_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_sm8350_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       oplus_sm8350_smt_test);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
					       oplus_sm8350_get_batt_vol);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
					       oplus_sm8350_get_batt_max);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
					       oplus_sm8350_get_batt_min);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
			oplus_sm8350_get_batt_curr);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
			oplus_sm8350_get_batt_temp);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
					       oplus_sm8350_get_batt_soc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
					       oplus_sm8350_get_batt_fcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
					       oplus_sm8350_get_batt_cc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
					       oplus_sm8350_get_batt_rm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
					       oplus_sm8350_get_batt_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
			oplus_sm8350_get_batt_auth);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
			oplus_sm8350_get_batt_hmac);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
			oplus_sm8350_set_batt_full);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0,
					       oplus_sm8350_update_dod0);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH,
			oplus_sm8350_update_soc_smooth_parameter);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS,
			oplus_sm8350_get_cb_status);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_LOCK:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_LOCK,
					       oplus_sm8350_set_lock);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_LOCKED:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_LOCKED,
					       oplus_sm8350_is_locked);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
					       oplus_sm8350_get_batt_num);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
			oplus_sm8350_get_device_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
			oplus_sm8350_get_device_type_for_vooc);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE,
			oplus_chg_8350_get_props_from_adsp_by_buffer);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_SUSPEND,
			oplus_chg_8350_gauge_is_suspend);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}
struct oplus_chg_ic_virq oplus_chg_8350_gauge_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
};

static int oplus_sm8350_ic_register(struct battery_chg_dev *bcdev)
{
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct device_node *child;
	struct oplus_chg_ic_dev *ic_dev = NULL;
	struct oplus_chg_ic_cfg ic_cfg;
	int rc;

	for_each_child_of_node(bcdev->dev->of_node, child) {
		rc = of_property_read_u32(child, "oplus,ic_type", &ic_type);
		if (rc < 0) {
			chg_err("can't get %s ic type, rc=%d\n", child->name, rc);
			continue;
		}
		rc = of_property_read_u32(child, "oplus,ic_index", &ic_index);
		if (rc < 0) {
			chg_err("can't get %s ic index, rc=%d\n", child->name, rc);
			continue;
		}
		ic_cfg.name = child->name;
		ic_cfg.index = ic_index;
		ic_cfg.type = ic_type;
		switch (ic_type) {
		case OPLUS_CHG_IC_BUCK:
			sprintf(ic_cfg.manu_name, "buck-sm8350");
			sprintf(ic_cfg.fw_id, "0x00");
			ic_cfg.get_func = oplus_chg_8350_buck_get_func;
			ic_cfg.virq_data = oplus_chg_8350_buck_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(oplus_chg_8350_buck_virq_table);
			break;
		case OPLUS_CHG_IC_GAUGE:
			sprintf(ic_cfg.manu_name, "adsp-gauge");
			sprintf(ic_cfg.fw_id, "0x00");
			ic_cfg.get_func = oplus_chg_8350_gauge_get_func;
			ic_cfg.virq_data = oplus_chg_8350_gauge_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(oplus_chg_8350_gauge_virq_table);
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_type);
			continue;
		}
		ic_dev = devm_oplus_chg_ic_register(bcdev->dev, &ic_cfg);
		if (!ic_dev) {
			rc = -ENODEV;
			chg_err("register %s error\n", child->name);
			continue;
		}
		chg_info("register %s\n", child->name);

		switch (ic_dev->type) {
		case OPLUS_CHG_IC_BUCK:
			bcdev->buck_ic = ic_dev;
			break;
		case OPLUS_CHG_IC_GAUGE:
			bcdev->gauge_ic = ic_dev;
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_dev->type);
			continue;
		}

		of_platform_populate(child, NULL, NULL, bcdev->dev);
	}

	return 0;
}

#endif /* OPLUS_FEATURE_CHG_BASIC */

static int battery_chg_probe(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	int rc, i;

#ifdef OPLUS_FEATURE_CHG_BASIC
	chg_info("battery_chg_probe start...\n");
#endif /*OPLUS_FEATURE_CHG_BASIC*/

	bcdev = devm_kzalloc(&pdev->dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

#ifdef OPLUS_FEATURE_CHG_BASIC
	g_bcdev = bcdev;
	bcdev->hvdcp_detect_time = 0;
	bcdev->hvdcp_detach_time = 0;
	bcdev->hvdcp_detect_ok = false;
	bcdev->hvdcp_disable = false;
	bcdev->voocphy_err_check = false;
#endif

	bcdev->psy_list[PSY_TYPE_BATTERY].map = battery_prop_map;
	bcdev->psy_list[PSY_TYPE_BATTERY].prop_count = BATT_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_get = BC_BATTERY_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_set = BC_BATTERY_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_USB].map = usb_prop_map;
	bcdev->psy_list[PSY_TYPE_USB].prop_count = USB_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_USB].opcode_get = BC_USB_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_USB].opcode_set = BC_USB_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_WLS].map = wls_prop_map;
	bcdev->psy_list[PSY_TYPE_WLS].prop_count = WLS_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_get = BC_WLS_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_set = BC_WLS_STATUS_SET;

	for (i = 0; i < PSY_TYPE_MAX; i++) {
		bcdev->psy_list[i].prop =
			devm_kcalloc(&pdev->dev, bcdev->psy_list[i].prop_count,
					sizeof(u32), GFP_KERNEL);
		if (!bcdev->psy_list[i].prop)
			return -ENOMEM;
	}

	bcdev->psy_list[PSY_TYPE_BATTERY].model =
		devm_kzalloc(&pdev->dev, MAX_STR_LEN, GFP_KERNEL);
	if (!bcdev->psy_list[PSY_TYPE_BATTERY].model)
		return -ENOMEM;

	mutex_init(&bcdev->rw_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
	mutex_init(&bcdev->oplus_custom_gpio.pinctrl_mutex);
	mutex_init(&bcdev->read_buffer_lock);
	init_completion(&bcdev->oem_read_ack);
#endif
	init_completion(&bcdev->ack);
	init_completion(&bcdev->fw_buf_ack);
	init_completion(&bcdev->fw_update_ack);
	INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	INIT_WORK(&bcdev->usb_type_work, battery_chg_update_usb_type_work);
#ifdef OPLUS_FEATURE_CHG_BASIC
	INIT_DELAYED_WORK(&bcdev->vooc_status_work, oplus_vooc_status_func);
	/* INIT_DELAYED_WORK(&bcdev->otg_init_work, oplus_otg_init_status_func); */
	/* INIT_DELAYED_WORK(&bcdev->adsp_crash_recover_work, oplus_adsp_crash_recover_func); */
	/* INIT_DELAYED_WORK(&bcdev->check_charger_out_work, oplus_check_charger_out_func); */
	/* INIT_DELAYED_WORK(&bcdev->voocphy_enable_check_work, oplus_voocphy_enable_check_func); */
	/* INIT_DELAYED_WORK(&bcdev->otg_vbus_enable_work, otg_notification_handler); */
	/* INIT_DELAYED_WORK(&bcdev->otg_status_check_work, oplus_otg_status_check_work); */
	/* INIT_DELAYED_WORK(&bcdev->vbus_adc_enable_work, oplus_vbus_enable_adc_work); */
	INIT_DELAYED_WORK(&bcdev->oem_lcm_en_check_work, oplus_oem_lcm_en_check_work);
	/* INIT_DELAYED_WORK(&bcdev->voocphy_err_work, oplus_voocphy_err_work); */
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	INIT_DELAYED_WORK(&bcdev->vchg_trig_work, oplus_vchg_trig_work);
	/* INIT_DELAYED_WORK(&bcdev->wait_wired_charge_on, oplus_wait_wired_charge_on_work); */
	/* INIT_DELAYED_WORK(&bcdev->wait_wired_charge_off, oplus_wait_wired_charge_off_work); */
#endif
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_UP);
	bcdev->dev = dev;

	client_data.id = MSG_OWNER_BC;
	client_data.name = "battery_charger";
	client_data.msg_cb = battery_chg_callback;
	client_data.priv = bcdev;
	client_data.state_cb = battery_chg_state_cb;

	bcdev->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(bcdev->client)) {
		rc = PTR_ERR(bcdev->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	bcdev->reboot_notifier.notifier_call = battery_chg_ship_mode;
	bcdev->reboot_notifier.priority = 255;
	register_reboot_notifier(&bcdev->reboot_notifier);

	rc = battery_chg_parse_dt(bcdev);
	if (rc < 0)
		goto error;

	bcdev->restrict_fcc_ua = DEFAULT_RESTRICT_FCC_UA;
	platform_set_drvdata(pdev, bcdev);
	bcdev->fake_soc = -EINVAL;
#ifndef OPLUS_FEATURE_CHG_BASIC
	rc = battery_chg_init_psy(bcdev);
	if (rc < 0)
		goto error;
#endif

	bcdev->battery_class.name = "qcom-battery";
	bcdev->battery_class.class_groups = battery_class_groups;
	rc = class_register(&bcdev->battery_class);
	if (rc < 0) {
		chg_err("Failed to create battery_class rc=%d\n", rc);
		goto error;
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_parse_custom_dt(bcdev);
#ifdef OPLUS_CHG_UNDEF
	main_psy = power_supply_get_by_name("main");
	if (main_psy) {
		pval.intval = 1000 * oplus_chg_get_fv(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_VOLTAGE_MAX,
				&pval);
		pval.intval = 1000 * oplus_chg_get_charging_current(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
				&pval);
	}
#endif
	/* oplus_chg_wake_update_work(); */

	if (oplus_vchg_trig_is_support() == true) {
		schedule_delayed_work(&bcdev->vchg_trig_work, msecs_to_jiffies(3000));
		oplus_vchg_trig_irq_register(bcdev);
	}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

	battery_chg_add_debugfs(bcdev);
	battery_chg_notify_enable(bcdev);
	device_init_wakeup(bcdev->dev, true);

#ifdef OPLUS_FEATURE_CHG_BASIC
	/* oplus_chg_set_match_temp_to_voocphy(); */
	/* oplus_voocphy_enable(true); */
	/* schedule_delayed_work(&bcdev->otg_init_work, 0); */
	/* schedule_delayed_work(&bcdev->voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(5000))); */
	init_debug_reg_proc(bcdev);

	rc = of_property_read_u32(dev->of_node, "oplus,batt_num", &bcdev->batt_num);
	if (rc < 0) {
		chg_err("can't get oplus,batt_num, rc=%d\n", rc);
		bcdev->batt_num = 1;
	}

	rc = oplus_sm8350_ic_register(bcdev);
	if (rc < 0)
		goto error;
	chg_info("battery_chg_probe end...\n");
#endif
	return 0;
error:
	pmic_glink_unregister_client(bcdev->client);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	return rc;
}

static int battery_chg_remove(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = platform_get_drvdata(pdev);
	int rc;

	device_init_wakeup(bcdev->dev, false);
	debugfs_remove_recursive(bcdev->debugfs_dir);
	class_unregister(&bcdev->battery_class);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	rc = pmic_glink_unregister_client(bcdev->client);
	if (rc < 0) {
		chg_err("Error unregistering from pmic_glink, rc=%d\n", rc);
		return rc;
	}
	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC

static void battery_chg_shutdown(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (bcdev) {
		chg_err("disable voocphy");
		/* cancel_delayed_work_sync(&bcdev->voocphy_enable_check_work); */
		oplus_typec_disable();
		/* oplus_voocphy_enable(false); */
	}

#ifdef OPLUS_CHG_UNDEF /* TODO */
	if (g_oplus_chip
		&& g_oplus_chip->chg_ops->charger_suspend
		&& g_oplus_chip->chg_ops->charger_unsuspend) {
		g_oplus_chip->chg_ops->charger_suspend();
		msleep(1000);
		g_oplus_chip->chg_ops->charger_unsuspend();
	}

	if (g_oplus_chip && g_oplus_chip->enable_shipmode) {
		smbchg_enter_shipmode(g_oplus_chip);
		msleep(1000);
	}
	if (!is_ext_chg_ops()) {
		bcdev->oem_misc_ctl_data = 0;
		bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_LCM_25K, false);
		oplus_oem_misc_ctl();
	}
#endif
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

static const struct of_device_id battery_chg_match_table[] = {
	{ .compatible = "oplus,hal_sm8350" },
	{},
};

static struct platform_driver battery_chg_driver = {
	.driver = {
		.name = "qti_battery_charger",
		.of_match_table = battery_chg_match_table,
#ifdef OPLUS_FEATURE_CHG_BASIC
		.pm	= &battery_chg_pm_ops,
#endif
	},
	.probe = battery_chg_probe,
	.remove = battery_chg_remove,
#ifdef OPLUS_FEATURE_CHG_BASIC
	.shutdown = battery_chg_shutdown,
#endif
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static int __init sm8350_chg_init(void)
{
	int ret;

	ret = platform_driver_register(&battery_chg_driver);
	return ret;
}

static void __exit sm8350_chg_exit(void)
{
	platform_driver_unregister(&battery_chg_driver);
}

oplus_chg_module_register(sm8350_chg);
#else
module_platform_driver(battery_chg_driver);
#endif

MODULE_DESCRIPTION("QTI Glink battery charger driver");
MODULE_LICENSE("GPL v2");
