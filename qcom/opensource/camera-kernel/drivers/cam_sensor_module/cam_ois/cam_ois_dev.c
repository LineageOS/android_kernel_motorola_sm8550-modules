// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "cam_ois_dev.h"
#include "cam_req_mgr_dev.h"
#include "cam_ois_soc.h"
#include "cam_ois_core.h"
#include "cam_debug_util.h"
#include "camera_main.h"
#include "cam_compat.h"

static struct cam_i3c_ois_data {
	struct cam_ois_ctrl_t                       *o_ctrl;
	struct completion                            probe_complete;
} g_i3c_ois_data[MAX_CAMERAS];

struct completion *cam_ois_get_i3c_completion(uint32_t index)
{
	return &g_i3c_ois_data[index].probe_complete;
}

static int cam_ois_subdev_close_internal(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh)
{
	struct cam_ois_ctrl_t *o_ctrl =
		v4l2_get_subdevdata(sd);

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "o_ctrl ptr is NULL");
			return -EINVAL;
	}

	mutex_lock(&(o_ctrl->ois_mutex));
	cam_ois_shutdown(o_ctrl);
	mutex_unlock(&(o_ctrl->ois_mutex));

	return 0;
}

static int cam_ois_subdev_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh)
{
	bool crm_active = cam_req_mgr_is_open();

	if (crm_active) {
		CAM_DBG(CAM_OIS, "CRM is ACTIVE, close should be from CRM");
		return 0;
	}

	return cam_ois_subdev_close_internal(sd, fh);
}

static long cam_ois_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	int                       rc     = 0;
	struct cam_ois_ctrl_t *o_ctrl = v4l2_get_subdevdata(sd);

	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		rc = cam_ois_driver_cmd(o_ctrl, arg);
		if (rc)
			CAM_ERR(CAM_OIS,
				"Failed with driver cmd: %d", rc);
		break;
	case CAM_SD_SHUTDOWN:
		if (!cam_req_mgr_is_shutdown()) {
			CAM_ERR(CAM_CORE, "SD shouldn't come from user space");
			return 0;
		}
		rc = cam_ois_subdev_close_internal(sd, NULL);
		break;
	default:
		CAM_ERR(CAM_OIS, "Wrong IOCTL cmd: %u", cmd);
		rc = -ENOIOCTLCMD;
		break;
	}

	return rc;
}

static int32_t cam_ois_update_i2c_info(struct cam_ois_ctrl_t *o_ctrl,
	struct cam_ois_i2c_info_t *i2c_info)
{
	struct cam_sensor_cci_client        *cci_client = NULL;

	if (o_ctrl->io_master_info.master_type == CCI_MASTER) {
		cci_client = o_ctrl->io_master_info.cci_client;
		if (!cci_client) {
			CAM_ERR(CAM_OIS, "failed: cci_client %pK",
				cci_client);
			return -EINVAL;
		}
		cci_client->cci_i2c_master = o_ctrl->cci_i2c_master;
		cci_client->sid = (i2c_info->slave_addr) >> 1;
		cci_client->retries = 3;
		cci_client->id_map = 0;
		cci_client->i2c_freq_mode = i2c_info->i2c_freq_mode;
	}

	return 0;
}

#ifdef CONFIG_COMPAT
static long cam_ois_init_subdev_do_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct cam_control cmd_data;
	int32_t rc = 0;

	if (copy_from_user(&cmd_data, (void __user *)arg,
		sizeof(cmd_data))) {
		CAM_ERR(CAM_OIS,
			"Failed to copy from user_ptr=%pK size=%zu",
			(void __user *)arg, sizeof(cmd_data));
		return -EFAULT;
	}

	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		rc = cam_ois_subdev_ioctl(sd, cmd, &cmd_data);
		if (rc) {
			CAM_ERR(CAM_OIS,
				"Failed in ois suddev handling rc %d",
				rc);
			return rc;
		}
		break;
	default:
		CAM_ERR(CAM_OIS, "Invalid compat ioctl: %d", cmd);
		rc = -ENOIOCTLCMD;
		break;
	}

	if (!rc) {
		if (copy_to_user((void __user *)arg, &cmd_data,
			sizeof(cmd_data))) {
			CAM_ERR(CAM_OIS,
				"Failed to copy from user_ptr=%pK size=%zu",
				(void __user *)arg, sizeof(cmd_data));
			rc = -EFAULT;
		}
	}
	return rc;
}
#endif

static const struct v4l2_subdev_internal_ops cam_ois_internal_ops = {
	.close = cam_ois_subdev_close,
};

static struct v4l2_subdev_core_ops cam_ois_subdev_core_ops = {
	.ioctl = cam_ois_subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = cam_ois_init_subdev_do_ioctl,
#endif
};

static struct v4l2_subdev_ops cam_ois_subdev_ops = {
	.core = &cam_ois_subdev_core_ops,
};

static int cam_ois_init_subdev_param(struct cam_ois_ctrl_t *o_ctrl)
{
	int rc = 0;

	o_ctrl->v4l2_dev_str.internal_ops = &cam_ois_internal_ops;
	o_ctrl->v4l2_dev_str.ops = &cam_ois_subdev_ops;
	strlcpy(o_ctrl->device_name, CAM_OIS_NAME,
		sizeof(o_ctrl->device_name));
	o_ctrl->v4l2_dev_str.name = o_ctrl->device_name;
	o_ctrl->v4l2_dev_str.sd_flags =
		(V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS);
	o_ctrl->v4l2_dev_str.ent_function = CAM_OIS_DEVICE_TYPE;
	o_ctrl->v4l2_dev_str.token = o_ctrl;
	 o_ctrl->v4l2_dev_str.close_seq_prior = CAM_SD_CLOSE_MEDIUM_PRIORITY;

	rc = cam_register_subdev(&(o_ctrl->v4l2_dev_str));
	if (rc)
		CAM_ERR(CAM_OIS, "fail to create subdev");

	return rc;
}

#ifdef CONFIG_DONGWOON_OIS_VSYNC
static int cam_ois_clear_data_ready(struct cam_ois_ctrl_t *o_ctrl)
{
	struct cam_sensor_i2c_reg_setting i2c_reg_setting = {NULL, 1, CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_WORD, 0};
	struct cam_sensor_i2c_reg_array i2c_write_settings = {DATA_READY_ADDR, 0x0000, 0, 0};
	int32_t rc = 0;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	i2c_reg_setting.reg_setting = &(i2c_write_settings);

	rc = camera_io_dev_write(&(o_ctrl->io_master_info), &(i2c_reg_setting));
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "Failed in Applying i2c wrt settings");
	}

	CAM_DBG(CAM_OIS,"Clear data-ready success");
	return rc;
}

static irqreturn_t cam_ois_vsync_irq_thread(int irq, void *data)
{
	struct cam_ois_ctrl_t *o_ctrl = data;
	int rc = 0, handled = IRQ_NONE, packet_cnt = 0, sample_cnt = 0;
	uint64_t qtime_ns;
	uint8_t *read_buff;
	uint32_t data_ready = 0xFFFF;
	int i = 0;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return IRQ_NONE;
	}

	if (o_ctrl->cam_ois_state < CAM_OIS_CONFIG) {
		CAM_WARN(CAM_OIS, "Not in right state to read Eis data: %d", o_ctrl->cam_ois_state);
		return IRQ_NONE;
	}

	if (o_ctrl->is_video_mode == false ||
		o_ctrl->is_need_eis_data == false) {
		CAM_DBG(CAM_OIS, "No need to read Eis data: %d %d", o_ctrl->is_video_mode, o_ctrl->is_need_eis_data);
		return IRQ_NONE;
	}

	if (!mutex_trylock(&o_ctrl->vsync_mutex)) {
		CAM_WARN(CAM_OIS, "try to get mutex fail, skip this irq");
		return IRQ_NONE;
	}

	rc = cam_sensor_util_get_current_qtimer_ns(&qtime_ns);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "failed to get qtimer rc:%d");
		goto release_mutex;
	}

	o_ctrl->prev_timestamp = o_ctrl->curr_timestamp;
	o_ctrl->curr_timestamp = qtime_ns;

	CAM_DBG(CAM_OIS, "vsync sof qtime timestamp: prev_timestamp: %lld, curr_timestamp: %lld",
				o_ctrl->prev_timestamp, o_ctrl->curr_timestamp);

	// when the first vsync arrived, return.
	if (o_ctrl->is_first_vsync) {
		o_ctrl->is_first_vsync = 0;
		rc = -EINVAL;
		goto release_mutex;
	}

	memset(o_ctrl->ois_data, 0, o_ctrl->ois_data_size);
	read_buff = o_ctrl->ois_data;

	do {
		if (packet_cnt > 0 && packet_cnt < MAX_PACKET)
			read_buff += PACKET_BYTE;

		// check 0x70DA = 1
		for (i = 0; i < READ_COUNT; i++) {
			rc = camera_io_dev_read(
				&o_ctrl->io_master_info,
				DATA_READY_ADDR,
				&data_ready,
				CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_WORD,
				false);

			if (rc < 0) {
				CAM_ERR(CAM_OIS, "failed to read OIS DATA_READY_ADDR reg rc: %d", rc);
				goto release_mutex;
			}

			if (data_ready == DATA_READY) {
				CAM_DBG(CAM_OIS, "data_ready == 0x0001, i = %d", i);
				break;
			} else if (data_ready != DATA_READY && i < (READ_COUNT - 1)) {
				CAM_DBG(CAM_OIS, "data_ready 0x%x != 0x0001, i = %d", data_ready, i);
				udelay(1000);
			} else {
				CAM_ERR(CAM_OIS, "data_ready 0x%x check fail, i = %d", data_ready, i);
				rc = -EINVAL;
				goto release_mutex;
			}
		}

		// read 1 packet data
		rc = camera_io_dev_read_seq(
			&o_ctrl->io_master_info,
			PACKET_ADDR,
			read_buff,
			CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_WORD,
			PACKET_BYTE);

		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Failed: seq read I2C settings: %d", rc);
			goto release_mutex;
		}

		if (packet_cnt == 0) {
			sample_cnt = read_buff[1];
			if (sample_cnt != 0) {
				packet_cnt = (sample_cnt + 9)/10;
				CAM_DBG(CAM_OIS,"sample_cnt = %d, packet_cnt = %d", sample_cnt, packet_cnt);

				// we only need max 49 samples and 5 packets.
				if (packet_cnt > MAX_PACKET || sample_cnt > (MAX_SAMPLE-1)) {
					CAM_WARN(CAM_OIS,"too many packets, skip this read, sample_cnt = %d, packet_cnt = %d",
								sample_cnt, packet_cnt);
					rc = -EINVAL;
					goto release_mutex;
				}
				o_ctrl->packet_count = packet_cnt;
			} else {
				CAM_WARN(CAM_OIS,"No-fatal: sample_cnt is 0, break the loop read");
				rc = -EINVAL;
				goto release_mutex;
			}
		}

		rc = cam_ois_clear_data_ready(o_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_OIS,"Write failed rc: %d", rc);
			goto release_mutex;
		}

		packet_cnt--;
	} while(packet_cnt > 0);

release_mutex:
	if (rc < 0) {
		memset(o_ctrl->ois_data, 0, o_ctrl->ois_data_size);
		o_ctrl->packet_count = 0;
		handled = IRQ_NONE;
	} else
		handled = IRQ_HANDLED;

	mutex_unlock(&(o_ctrl->vsync_mutex));

	if (rc >= 0)
		complete(&o_ctrl->vsync_completion);

	return handled;
}
#endif

static int cam_ois_i2c_component_bind(struct device *dev,
	struct device *master_dev, void *data)
{
	int                          rc = 0;
	struct i2c_client           *client = NULL;
	struct cam_ois_ctrl_t       *o_ctrl = NULL;
	struct cam_ois_soc_private  *soc_private = NULL;

	client = container_of(dev, struct i2c_client, dev);
	if (client == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args client: %pK",
			client);
		return -EINVAL;
	}

	o_ctrl = kzalloc(sizeof(*o_ctrl), GFP_KERNEL);
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "kzalloc failed");
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, o_ctrl);

	o_ctrl->soc_info.dev = &client->dev;
	o_ctrl->soc_info.dev_name = client->name;
	o_ctrl->ois_device_type = MSM_CAMERA_I2C_DEVICE;
	o_ctrl->io_master_info.master_type = I2C_MASTER;
	o_ctrl->io_master_info.client = client;

	soc_private = kzalloc(sizeof(struct cam_ois_soc_private),
		GFP_KERNEL);
	if (!soc_private) {
		rc = -ENOMEM;
		goto octrl_free;
	}

	o_ctrl->soc_info.soc_private = soc_private;
	rc = cam_ois_driver_soc_init(o_ctrl);
	if (rc) {
		CAM_ERR(CAM_OIS, "failed: cam_sensor_parse_dt rc %d", rc);
		goto soc_free;
	}

	rc = cam_ois_init_subdev_param(o_ctrl);
	if (rc)
		goto soc_free;

	o_ctrl->cam_ois_state = CAM_OIS_INIT;

	return rc;

soc_free:
	kfree(soc_private);
octrl_free:
	kfree(o_ctrl);
probe_failure:
	return rc;
}

static void cam_ois_i2c_component_unbind(struct device *dev,
	struct device *master_dev, void *data)
{
	int                             i;
	struct i2c_client              *client = NULL;
	struct cam_ois_ctrl_t          *o_ctrl = NULL;
	struct cam_hw_soc_info         *soc_info;
	struct cam_ois_soc_private     *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	client = container_of(dev, struct i2c_client, dev);
	if (!client) {
		CAM_ERR(CAM_OIS,
			"Failed to get i2c client");
		return;
	}

	o_ctrl = i2c_get_clientdata(client);
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "ois device is NULL");
		return;
	}

	CAM_INFO(CAM_OIS, "i2c driver remove invoked");
	soc_info = &o_ctrl->soc_info;

	for (i = 0; i < soc_info->num_clk; i++)
		devm_clk_put(soc_info->dev, soc_info->clk[i]);

	mutex_lock(&(o_ctrl->ois_mutex));
	cam_ois_shutdown(o_ctrl);
	mutex_unlock(&(o_ctrl->ois_mutex));
	cam_unregister_subdev(&(o_ctrl->v4l2_dev_str));

	soc_private =
		(struct cam_ois_soc_private *)soc_info->soc_private;
	power_info = &soc_private->power_info;

	kfree(o_ctrl->soc_info.soc_private);
	v4l2_set_subdevdata(&o_ctrl->v4l2_dev_str.sd, NULL);
	kfree(o_ctrl);
}

const static struct component_ops cam_ois_i2c_component_ops = {
	.bind = cam_ois_i2c_component_bind,
	.unbind = cam_ois_i2c_component_unbind,
};

static int cam_ois_i2c_driver_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc = 0;

	if (client == NULL || id == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args client: %pK id: %pK",
			client, id);
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		CAM_ERR(CAM_OIS, "%s :: i2c_check_functionality failed",
			client->name);
		return -EFAULT;
	}

	CAM_DBG(CAM_OIS, "Adding sensor ois component");
	rc = component_add(&client->dev, &cam_ois_i2c_component_ops);
	if (rc)
		CAM_ERR(CAM_OIS, "failed to add component rc: %d", rc);

	return rc;
}

static int cam_ois_i2c_driver_remove(struct i2c_client *client)
{
	component_del(&client->dev, &cam_ois_i2c_component_ops);

	return 0;
}

static int cam_ois_component_bind(struct device *dev,
	struct device *master_dev, void *data)
{
	int32_t                         rc = 0;
	struct cam_ois_ctrl_t          *o_ctrl = NULL;
	struct cam_ois_soc_private     *soc_private = NULL;
	bool                            i3c_i2c_target;
	struct platform_device *pdev = to_platform_device(dev);

	i3c_i2c_target = of_property_read_bool(pdev->dev.of_node, "i3c-i2c-target");
	if (i3c_i2c_target)
		return 0;

	o_ctrl = kzalloc(sizeof(struct cam_ois_ctrl_t), GFP_KERNEL);
	if (!o_ctrl)
		return -ENOMEM;

	o_ctrl->soc_info.pdev = pdev;
	o_ctrl->pdev = pdev;
	o_ctrl->soc_info.dev = &pdev->dev;
	o_ctrl->soc_info.dev_name = pdev->name;

	o_ctrl->ois_device_type = MSM_CAMERA_PLATFORM_DEVICE;

	o_ctrl->io_master_info.master_type = CCI_MASTER;
	o_ctrl->io_master_info.cci_client = kzalloc(
		sizeof(struct cam_sensor_cci_client), GFP_KERNEL);
	if (!o_ctrl->io_master_info.cci_client)
		goto free_o_ctrl;

	soc_private = kzalloc(sizeof(struct cam_ois_soc_private),
		GFP_KERNEL);
	if (!soc_private) {
		rc = -ENOMEM;
		goto free_cci_client;
	}
	o_ctrl->soc_info.soc_private = soc_private;
	soc_private->power_info.dev  = &pdev->dev;

	INIT_LIST_HEAD(&(o_ctrl->i2c_init_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_preprog_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_precoeff_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_calib_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_fwinit_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_postcalib_data.list_head));
	INIT_LIST_HEAD(&(o_ctrl->i2c_mode_data.list_head));
#ifdef CONFIG_MOT_OIS_AF_DRIFT
	INIT_LIST_HEAD(&(o_ctrl->i2c_af_drift_data.list_head));
#endif
#ifdef CONFIG_MOT_OIS_AFTER_SALES_SERVICE
	INIT_LIST_HEAD(&(o_ctrl->i2c_gyro_data.list_head));
#endif
	INIT_LIST_HEAD(&(o_ctrl->i2c_time_data.list_head));
	mutex_init(&(o_ctrl->ois_mutex));
	rc = cam_ois_driver_soc_init(o_ctrl);
	if (rc) {
		CAM_ERR(CAM_OIS, "failed: soc init rc %d", rc);
		goto free_soc;
	}

	rc = cam_ois_init_subdev_param(o_ctrl);
	if (rc)
		goto free_soc;

	rc = cam_ois_update_i2c_info(o_ctrl, &soc_private->i2c_info);
	if (rc) {
		CAM_ERR(CAM_OIS, "failed: to update i2c info rc %d", rc);
		goto unreg_subdev;
	}
	o_ctrl->bridge_intf.device_hdl = -1;

	platform_set_drvdata(pdev, o_ctrl);
	o_ctrl->cam_ois_state = CAM_OIS_INIT;

	g_i3c_ois_data[o_ctrl->soc_info.index].o_ctrl = o_ctrl;
	init_completion(&g_i3c_ois_data[o_ctrl->soc_info.index].probe_complete);

#ifdef CONFIG_DONGWOON_OIS_VSYNC
	mutex_init(&(o_ctrl->vsync_mutex));
	init_completion(&o_ctrl->vsync_completion);

	o_ctrl->ois_data_size = (PACKET_BYTE*MAX_PACKET+1);
	o_ctrl->ois_data = kzalloc(o_ctrl->ois_data_size, GFP_KERNEL);
	if (!o_ctrl->ois_data) {
		rc = -ENOMEM;
		goto unreg_subdev;
	}

	if (o_ctrl->is_ois_vsync_irq_supported) {
		o_ctrl->vsync_irq = platform_get_irq_optional(pdev, 0);

		if (o_ctrl->vsync_irq > 0) {
			CAM_INFO(CAM_OIS, "get ois-vsync irq: %d", o_ctrl->vsync_irq);
			rc = devm_request_threaded_irq(dev,
							o_ctrl->vsync_irq,
							NULL,
							cam_ois_vsync_irq_thread,
							(IRQF_TRIGGER_RISING | IRQF_ONESHOT),
							"ois-vsync-irq",
							o_ctrl);
			if (rc != 0)
				CAM_ERR(CAM_OIS, "failed: to request ois-vsync irq %d, rc %d", o_ctrl->vsync_irq, rc);
			else
				CAM_INFO(CAM_OIS, "request ois-vsync irq success");
		} else
			CAM_ERR(CAM_OIS, "failed: to get ois-vsync irq");
	}
#endif

	CAM_DBG(CAM_OIS, "Component bound successfully");
	return rc;
unreg_subdev:
	cam_unregister_subdev(&(o_ctrl->v4l2_dev_str));
free_soc:
	kfree(soc_private);
free_cci_client:
	kfree(o_ctrl->io_master_info.cci_client);
free_o_ctrl:
	kfree(o_ctrl);
	return rc;
}

static void cam_ois_component_unbind(struct device *dev,
	struct device *master_dev, void *data)
{
	int                             i;
	struct cam_ois_ctrl_t          *o_ctrl;
	struct cam_ois_soc_private     *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info         *soc_info;
	bool                            i3c_i2c_target;
	struct platform_device *pdev = to_platform_device(dev);

	i3c_i2c_target = of_property_read_bool(pdev->dev.of_node, "i3c-i2c-target");
	if (i3c_i2c_target)
		return;

	o_ctrl = platform_get_drvdata(pdev);
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "ois device is NULL");
		return;
	}

	CAM_INFO(CAM_OIS, "platform driver remove invoked");
	soc_info = &o_ctrl->soc_info;
	for (i = 0; i < soc_info->num_clk; i++)
		devm_clk_put(soc_info->dev, soc_info->clk[i]);

	mutex_lock(&(o_ctrl->ois_mutex));
	cam_ois_shutdown(o_ctrl);
	mutex_unlock(&(o_ctrl->ois_mutex));
	cam_unregister_subdev(&(o_ctrl->v4l2_dev_str));

	soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

#ifdef CONFIG_DONGWOON_OIS_VSYNC
	kfree(o_ctrl->ois_data);
#endif
	kfree(o_ctrl->soc_info.soc_private);
	kfree(o_ctrl->io_master_info.cci_client);
	platform_set_drvdata(pdev, NULL);
	v4l2_set_subdevdata(&o_ctrl->v4l2_dev_str.sd, NULL);
	kfree(o_ctrl);
}

const static struct component_ops cam_ois_component_ops = {
	.bind = cam_ois_component_bind,
	.unbind = cam_ois_component_unbind,
};

static int32_t cam_ois_platform_driver_probe(
	struct platform_device *pdev)
{
	int rc = 0;

	CAM_DBG(CAM_OIS, "Adding OIS Sensor component");
	rc = component_add(&pdev->dev, &cam_ois_component_ops);
	if (rc)
		CAM_ERR(CAM_OIS, "failed to add component rc: %d", rc);

	return rc;
}

static int cam_ois_platform_driver_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &cam_ois_component_ops);
	return 0;
}

static const struct of_device_id cam_ois_dt_match[] = {
	{ .compatible = "qcom,ois" },
	{ }
};

static const struct of_device_id cam_ois_i2c_dt_match[] = {
	{ .compatible = "qcom,cam-i2c-ois" },
	{ }
};

MODULE_DEVICE_TABLE(of, cam_ois_dt_match);
MODULE_DEVICE_TABLE(of, cam_ois_i2c_dt_match);

struct platform_driver cam_ois_platform_driver = {
	.driver = {
		.name = "qcom,ois",
		.owner = THIS_MODULE,
		.of_match_table = cam_ois_dt_match,
	},
	.probe = cam_ois_platform_driver_probe,
	.remove = cam_ois_platform_driver_remove,
};
static const struct i2c_device_id cam_ois_i2c_id[] = {
	{ OIS_DRIVER_I2C, (kernel_ulong_t)NULL},
	{ }
};

struct i2c_driver cam_ois_i2c_driver = {
	.id_table = cam_ois_i2c_id,
	.probe  = cam_ois_i2c_driver_probe,
	.remove = cam_ois_i2c_driver_remove,
	.driver = {
		.name = OIS_DRIVER_I2C,
		.owner = THIS_MODULE,
		.of_match_table = cam_ois_i2c_dt_match,
		.suppress_bind_attrs = true,
	},
};

static struct i3c_device_id ois_i3c_id[MAX_I3C_DEVICE_ID_ENTRIES + 1];

static int cam_ois_i3c_driver_probe(struct i3c_device *client)
{
	int32_t rc = 0;
	struct cam_ois_ctrl_t            *o_ctrl = NULL;
	uint32_t                          index;
	struct device                    *dev;

	if (!client) {
		CAM_INFO(CAM_OIS, "Null Client pointer");
		return -EINVAL;
	}

	dev = &client->dev;

	CAM_DBG(CAM_OIS, "Probe for I3C Slave %s", dev_name(dev));

	rc = of_property_read_u32(dev->of_node, "cell-index", &index);
	if (rc) {
		CAM_ERR(CAM_OIS, "device %s failed to read cell-index", dev_name(dev));
		return rc;
	}

	if (index >= MAX_CAMERAS) {
		CAM_ERR(CAM_OIS, "Invalid Cell-Index: %u for %s", index, dev_name(dev));
		return -EINVAL;
	}

	o_ctrl = g_i3c_ois_data[index].o_ctrl;
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "o_ctrl is null. I3C Probe before platfom driver probe for %s",
			dev_name(dev));
		return -EINVAL;
	}

	o_ctrl->io_master_info.i3c_client = client;

	complete_all(&g_i3c_ois_data[index].probe_complete);

	CAM_DBG(CAM_OIS, "I3C Probe Finished for %s", dev_name(dev));
	return rc;
}

static struct i3c_driver cam_ois_i3c_driver = {
	.id_table = ois_i3c_id,
	.probe = cam_ois_i3c_driver_probe,
	.remove = cam_i3c_driver_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = OIS_DRIVER_I3C,
		.of_match_table = cam_ois_dt_match,
		.suppress_bind_attrs = true,
	},
};

int cam_ois_driver_init(void)
{
	int rc = 0;
	struct device_node                      *dev;
	int num_entries = 0;

	rc = platform_driver_register(&cam_ois_platform_driver);
	if (rc) {
		CAM_ERR(CAM_OIS, "platform_driver_register failed rc = %d", rc);
		return rc;
	}

	rc = i2c_add_driver(&cam_ois_i2c_driver);
	if (rc) {
		CAM_ERR(CAM_OIS, "i2c_add_driver failed rc = %d", rc);
		goto i2c_register_err;
	}

	memset(ois_i3c_id, 0, sizeof(struct i3c_device_id) * (MAX_I3C_DEVICE_ID_ENTRIES + 1));

	dev = of_find_node_by_path(I3C_SENSOR_DEV_ID_DT_PATH);
	if (!dev) {
		CAM_DBG(CAM_OIS, "Couldnt Find the i3c-id-table dev node");
		return 0;
	}

	rc = cam_sensor_count_elems_i3c_device_id(dev, &num_entries,
		"i3c-ois-id-table");
	if (rc)
		return 0;

	rc = cam_sensor_fill_i3c_device_id(dev, num_entries,
		"i3c-ois-id-table", ois_i3c_id);
	if (rc)
		goto i3c_register_err;

	rc = i3c_driver_register_with_owner(&cam_ois_i3c_driver, THIS_MODULE);
	if (rc) {
		CAM_ERR(CAM_OIS, "i3c_driver registration failed, rc: %d", rc);
		goto i3c_register_err;
	}

	return 0;

i3c_register_err:
	i2c_del_driver(&cam_ois_i2c_driver);
i2c_register_err:
	platform_driver_unregister(&cam_ois_platform_driver);

	return rc;
}

void cam_ois_driver_exit(void)
{
	struct device_node *dev;

	platform_driver_unregister(&cam_ois_platform_driver);
	i2c_del_driver(&cam_ois_i2c_driver);

	dev = of_find_node_by_path(I3C_SENSOR_DEV_ID_DT_PATH);
	if (!dev) {
		CAM_DBG(CAM_EEPROM, "Couldnt Find the i3c-id-table dev node");
		return;
	}

	i3c_driver_unregister(&cam_ois_i3c_driver);
}

MODULE_DESCRIPTION("CAM OIS driver");
MODULE_LICENSE("GPL v2");
