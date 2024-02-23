/*
 * Copyright (C) 2023 Motorola Mobility LLC.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/firmware.h>
#include "cam_ois_core.h"
#include "cam_ois_soc.h"

#define REG_APP_VER (0x1008)
#define REG_OIS_STS (0x0001)
#define REG_OIS_CTRL (0x0000)
#define REG_AF_STS (0x0201)
#define REG_AF_CTRL (0x0200)
#define REG_FWUP_CTRL (0x1000)
#define REG_DATA_BUF (0x1100)
#define REG_FWUP_CHKSUM (0x1002)
#define REG_FWUP_ERR (0x1001)

#define STATE_READY (0x01)
#define OIS_OFF (0x00)
#define AF_OFF (0x00)
#define NO_ERROR (0x00)
#define RESET_REQ (0x80)
#define FWUP_CTRL_256_SET (0x07)

#define SEM1217S_CHUNCK_SIZE (256)

//#define SEM1217S_OIS_DEBUG

static int32_t sem1217s_cci_write_u16_little_endian(struct camera_io_master * io_master_info, uint16_t reg, uint16_t val)
{
	int32_t rc = 0;
	struct cam_sensor_i2c_reg_array reg_setting;
	struct cam_sensor_i2c_reg_setting wr_setting;

	uint16_t send = 0;
	send = (val << 8) | (val >> 8);

	CAM_INFO(CAM_OIS, "val 0x%x, send 0x%x", val, send);

	reg_setting.reg_addr = reg;
	reg_setting.reg_data = send;
	reg_setting.delay = 0;
	reg_setting.data_mask = 0;

	wr_setting.reg_setting = &reg_setting;
	wr_setting.size = 1;
	wr_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
	wr_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
	wr_setting.delay = 0;

	rc = camera_io_dev_write(io_master_info, &wr_setting);
	return rc;
}

static int32_t sem1217s_cci_write_byte(struct camera_io_master * io_master_info, uint16_t reg, uint8_t val)
{
	int32_t rc = 0;
	struct cam_sensor_i2c_reg_array reg_setting;
	struct cam_sensor_i2c_reg_setting wr_setting;

	reg_setting.reg_addr = reg;
	reg_setting.reg_data = val;
	reg_setting.delay = 0;
	reg_setting.data_mask = 0;

	wr_setting.reg_setting = &reg_setting;
	wr_setting.size = 1;
	wr_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
	wr_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	wr_setting.delay = 0;

	rc = camera_io_dev_write(io_master_info, &wr_setting);
	return rc;
}

static int32_t sem1217s_cci_read_byte(struct camera_io_master * io_master_info, uint16_t reg, uint8_t *val)
{
	int32_t rc = 0;
	uint32_t regVal = 0;
	rc = camera_io_dev_read(io_master_info, reg, &regVal, CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_BYTE, false);
	if (!rc) {
		*val = (uint8_t)regVal;
	}
	return rc;
}

static int32_t sem1217s_cci_read_u32_little_endian(struct camera_io_master * io_master_info, uint16_t reg, uint32_t *receive)
{
	int32_t rc = 0;
	uint32_t val = 0;
	rc = camera_io_dev_read(io_master_info, reg, &val, CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD, false);
	if (!rc) {
		*receive = 	((val & 0xFF000000) >> 24)|
				((val & 0x00FF0000) >> 8) |
				((val & 0x0000FF00) << 8) |
				((val & 0x000000FF) << 24);

		CAM_INFO(CAM_OIS, "val 0x%x, receive 0x%x", val, *receive);
	}
	return rc;
}

static void sem1217s_delay_ms(uint32_t ms)
{
	usleep_range(ms*1000, ms*1000+10);
	return;
}

int32_t sem1217s_fw_update(struct cam_ois_ctrl_t *o_ctrl, const struct firmware *fw)
{
	struct cam_sensor_i2c_reg_setting i2c_register_setting = {0};
	struct cam_sensor_i2c_reg_array   i2c_register_array[SEM1217S_CHUNCK_SIZE] = {0};
	struct cam_sensor_i2c_reg_array   *pI2CRegisterArray = NULL;
	uint32_t wr_bytes = 0;
	uint32_t remain_bytes = 0;
	uint8_t *pFwData = NULL;
	uint32_t current_fw_ver = 0;
	uint32_t new_fw_ver = 0;
	uint32_t updated_ver = 0;
	int32_t rc = 0;
	uint8_t reg_val = 0;
	uint16_t check_sum = 0;
	uint8_t buff[SEM1217S_CHUNCK_SIZE] = {0};
	int32_t i = 0;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid o_ctrl args");
		return -EINVAL;
	}

	if (fw == NULL) {
		CAM_ERR(CAM_OIS, "Invalid fw args");
		return -EINVAL;
	}

	if (fw->size <= 0 || fw->data == NULL) {
		CAM_ERR(CAM_OIS, "FW is not valid( buf:%p, size:%d)", fw->data, fw->size);
		return -EINVAL;
	}

	pFwData = (uint8_t *)fw->data;

	rc = sem1217s_cci_read_u32_little_endian(&(o_ctrl->io_master_info), REG_APP_VER, &current_fw_ver);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "Read current fw version failed, rc %d", rc);
		return -EINVAL;
	}

	CAM_INFO(CAM_OIS, "OIS current_fw_ver 0x%x, fw->size %d", current_fw_ver, fw->size);

	new_fw_ver = *(uint32_t *)(pFwData + (fw->size - 12));

	if (current_fw_ver == new_fw_ver) {
		CAM_INFO(CAM_OIS, "Skip FW upgrade, current_fw_ver 0x%x, new_fw_ver 0x%x", current_fw_ver, new_fw_ver);
		return 0;
	}

	CAM_INFO(CAM_OIS, "OIS new_fw_ver 0x%x", new_fw_ver);

	if (current_fw_ver != 0) {
		reg_val = 0;
		rc = sem1217s_cci_read_byte(&(o_ctrl->io_master_info), REG_OIS_STS, &reg_val);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Read REG_OIS_STS failed, rc %d", rc);
			return -EINVAL;
		}

		if (reg_val != STATE_READY) {
			rc = sem1217s_cci_write_byte(&(o_ctrl->io_master_info), REG_OIS_CTRL, OIS_OFF);
			if (rc < 0) {
				CAM_ERR(CAM_OIS, "Write REG_OIS_CTRL failed, rc %d", rc);
				return -EINVAL;
			}
		}

		reg_val = 0;
		rc = sem1217s_cci_read_byte(&(o_ctrl->io_master_info), REG_AF_STS, &reg_val);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Read REG_AF_STS failed, rc %d", rc);
			return -EINVAL;
		}

		if (reg_val != STATE_READY) {
			rc = sem1217s_cci_write_byte(&(o_ctrl->io_master_info), REG_AF_CTRL, AF_OFF);
			if (rc < 0) {
				CAM_ERR(CAM_OIS, "Write REG_AF_CTRL failed, rc %d", rc);
				return -EINVAL;
			}
		}
	}

	rc = sem1217s_cci_write_byte(&(o_ctrl->io_master_info), REG_FWUP_CTRL, FWUP_CTRL_256_SET);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "Write REG_FWUP_CTRL failed, rc %d", rc);
		return -EINVAL;
	}
	sem1217s_delay_ms(60);

	remain_bytes = fw->size;
	check_sum = 0;

	CAM_INFO(CAM_OIS, "OIS FW download start, fw->size %d", fw->size);

	while (remain_bytes) {
		pI2CRegisterArray = i2c_register_array;
		wr_bytes = (remain_bytes >= SEM1217S_CHUNCK_SIZE) ? SEM1217S_CHUNCK_SIZE : remain_bytes;
		remain_bytes -= wr_bytes;
		i2c_register_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		i2c_register_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		i2c_register_setting.reg_setting = i2c_register_array;
		i2c_register_setting.size = wr_bytes;

		for (i = 0; (i < wr_bytes) && (pFwData != NULL) && (pI2CRegisterArray != NULL) && (i < SEM1217S_CHUNCK_SIZE); i++) {
			pI2CRegisterArray->reg_addr = REG_DATA_BUF;
			pI2CRegisterArray->reg_data = *pFwData;
			buff[i] = *pFwData;
			pI2CRegisterArray->delay = 0;
			pI2CRegisterArray->data_mask = 0;

			if (i % 2 != 0) {
#ifdef SEM1217S_OIS_DEBUG
				CAM_INFO(CAM_OIS, "OIS FW download debug i %d, wr_bytes %d, remain_bytes %d", i, wr_bytes, remain_bytes);
				CAM_INFO(CAM_OIS, "OIS FW download debug buff[%d] = 0x%x ", i-1, buff[i-1]);
				CAM_INFO(CAM_OIS, "OIS FW download debug buff[%d] = 0x%x ", i, buff[i]);
				CAM_INFO(CAM_OIS, "OIS FW download debug check_sum = 0x%x ", check_sum);
#endif
				check_sum += ((buff[i] << 8) | buff[i-1]);
			}

			pI2CRegisterArray++;
			pFwData++;
		}

		rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info), &i2c_register_setting, CAM_SENSOR_I2C_WRITE_BURST);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "FW Download error. rc (%d)", rc);
			return -EINVAL;
		}
		sem1217s_delay_ms(10);
	}

	rc = sem1217s_cci_write_u16_little_endian(&(o_ctrl->io_master_info), REG_FWUP_CHKSUM, check_sum);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "Write REG_FWUP_CHKSUM failed, rc %d", rc);
		return -EINVAL;
	}
	sem1217s_delay_ms(200);

	reg_val = 0;
	rc = sem1217s_cci_read_byte(&(o_ctrl->io_master_info), REG_FWUP_ERR, &reg_val);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "Read REG_FWUP_ERR failed, rc %d", rc);
		return -EINVAL;
	}

	if (reg_val != NO_ERROR) {
		CAM_ERR(CAM_OIS, "OIS FW download Error 0x%x", reg_val);
		return -EINVAL;
	}

	rc = sem1217s_cci_write_byte(&(o_ctrl->io_master_info), REG_FWUP_CTRL, RESET_REQ);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "Write REG_FWUP_CTRL failed, rc %d", rc);
		return -EINVAL;
	}
	sem1217s_delay_ms(200);

	rc = sem1217s_cci_read_u32_little_endian(&(o_ctrl->io_master_info), REG_APP_VER, &updated_ver);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "Read updated_ver fw version failed, rc %d", rc);
		return -EINVAL;
	}

	CAM_INFO(CAM_OIS, "OIS updated_ver 0x%x", updated_ver);

	if (updated_ver != new_fw_ver) /* Compare updated_ver and new_fw_ver */
	{
		CAM_ERR(CAM_OIS, "OIS FW download Error in FW version");
		return -EINVAL;
	}

	CAM_INFO(CAM_OIS, "OIS FW download Success done");
	return 0;
}
