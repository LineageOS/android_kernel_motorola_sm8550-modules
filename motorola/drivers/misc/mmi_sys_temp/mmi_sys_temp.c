/*
 * Copyright (C) 2015, 2018 Motorola Mobility LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mmi_sys_temp.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/uaccess.h>
#include <linux/power_supply.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/compat.h>

#define TEMP_NODE_SENSOR_NAMES "mmi,temperature-names"
#define SENSOR_LISTENER_NAMES "mmi,sensor-listener-names"
#define NEED_UEVENT_SENSOR_NAMES "mmi,need-uevent-sensors"
#define UEVENT_TEMP_THRESHOLD "mmi,uevent-temp-threshold"
#define DEFAULT_UEVENT_TEMP_THRESHOLD 40000

#define DEFAULT_TEMPERATURE 0

struct mmi_sys_temp_sensor {
	struct thermal_zone_device *tz_dev;
	const char *name;
	int temp;
	bool need_uevent;
	int pre_temp;
};

struct mmi_sys_temp_dev {
	struct platform_device *pdev;
	int num_sensors;
	int num_sensors_listener;
	int uevent_temp_threshold;
	struct mmi_sys_temp_sensor *sensor;
	struct mmi_sys_temp_sensor *sensor_listener;
	struct notifier_block psy_nb;
	struct work_struct psy_changed_work;
};

static struct mmi_sys_temp_dev *sys_temp_dev;

static int uevent_generate(struct mmi_sys_temp_dev *data, int index)
{
	struct kobj_uevent_env *env;
	int ret;

	if ((!data->sensor[index].need_uevent) ||
			(data->sensor[index].temp < data->uevent_temp_threshold))
		return 0;

	//compare unit C
	if ((data->sensor[index].temp / 1000) == (data->sensor[index].pre_temp / 1000))
		return 0;

	env = devm_kzalloc(&data->pdev->dev, sizeof(*env), GFP_KERNEL);
	if (!env) {
		dev_err(&data->pdev->dev, "%s: alloc uevent error\n", __func__);
		return -1;
	}

	data->sensor[index].pre_temp = data->sensor[index].temp;

	add_uevent_var(env, "NAME=%s", data->sensor[index].name);
	add_uevent_var(env, "TEMP=%d", data->sensor[index].temp);
	add_uevent_var(env, "TRIP=%d", 0);

	ret = kobject_uevent_env(&data->sensor[index].tz_dev->device.kobj, KOBJ_CHANGE, env->envp);

	devm_kfree(&data->pdev->dev, env);

	dev_info(&data->pdev->dev, "trigger uevent index %i, temp %d pre_temp %d",
			index, data->sensor[index].temp, data->sensor[index].pre_temp);

	return ret;
}

static int uevent_parse_dt(struct mmi_sys_temp_dev *data, struct device_node *node, int num_sensors)
{
	const char* name;
	int num_need_uevent_sensors, i;
	int ret = 0;

	num_need_uevent_sensors = of_property_count_strings(node, NEED_UEVENT_SENSOR_NAMES);
	if (num_need_uevent_sensors <= 0) {
		dev_info(&data->pdev->dev, "No sensor need uevent\n");
		return ret;
	}

	if (of_property_read_u32(node, UEVENT_TEMP_THRESHOLD, &data->uevent_temp_threshold))
		data->uevent_temp_threshold = DEFAULT_UEVENT_TEMP_THRESHOLD;

	for (i=0; i<num_need_uevent_sensors; i++) {
		int n;

		ret = of_property_read_string_index(node,
						NEED_UEVENT_SENSOR_NAMES, i,
						&name);
		if (ret) {
			dev_err(&data->pdev->dev, "Unable to read of_prop string of uevent sensors\n");
			break;
		}

		dev_info(&data->pdev->dev, "%s need uevent, thresh %d\n", name, data->uevent_temp_threshold);
		for(n=0; n<num_sensors; n++) {
			if (strstr(data->sensor[n].name, name)) {
				dev_info(&data->pdev->dev, "%s uevent flag match!\n", name);
				data->sensor[n].need_uevent = true;
				break;
			}
		}
	}

	return ret;
}

static int mmi_sys_temp_ioctl_open(struct inode *node, struct file *file)
{
	return 0;
}

static int mmi_sys_temp_ioctl_release(struct inode *node, struct file *file)
{
	return 0;
}

static long mmi_sys_temp_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	int i;
	int ret = 0;
	struct mmi_sys_temp_ioctl request;

	if (!sys_temp_dev)
		return -EINVAL;

	if ((_IOC_TYPE(cmd) != MMI_SYS_TEMP_MAGIC_NUM) ||
	    (_IOC_NR(cmd) >= MMI_SYS_TEMP_MAX_NUM)) {
		return -ENOTTY;
	}

	switch (cmd) {
	case MMI_SYS_TEMP_SET_TEMP:
		ret = copy_from_user(&request, (void __user *)arg,
				     sizeof(struct mmi_sys_temp_ioctl));
		if (ret) {
			dev_err(&sys_temp_dev->pdev->dev,
				"failed to copy_from_user\n");
			return -EACCES;
		}

		dev_dbg(&sys_temp_dev->pdev->dev, "name=%s, temperature=%d\n",
			request.name, request.temperature);

		for (i = 0; i < sys_temp_dev->num_sensors; i++) {
			if (!strncasecmp(sys_temp_dev->sensor[i].name,
					 request.name,
					 MMI_SYS_TEMP_NAME_LENGTH)) {
				sys_temp_dev->sensor[i].temp =
					request.temperature;

				/*generate sensor uevent*/
				uevent_generate(sys_temp_dev, i);
				break;
			}
		}
		if (i >= sys_temp_dev->num_sensors) {
			dev_dbg(&sys_temp_dev->pdev->dev,
				"name %s not supported\n",
			request.name);
			ret = -EBADR;
		}
		break;
	default:
		break;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
static long mmi_sys_temp_compat_ioctl(struct file *file,
				      unsigned int cmd, unsigned long arg)
{
	arg = (unsigned long)compat_ptr(arg);
	return mmi_sys_temp_ioctl(file, cmd, arg);
}
#endif	/* CONFIG_COMPAT */

static const struct file_operations mmi_sys_temp_fops = {
	.owner = THIS_MODULE,
	.open = mmi_sys_temp_ioctl_open,
	.unlocked_ioctl = mmi_sys_temp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = mmi_sys_temp_compat_ioctl,
#endif  /* CONFIG_COMPAT */
	.release = mmi_sys_temp_ioctl_release,
};

static struct miscdevice mmi_sys_temp_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mmi_sys_temp",
	.fops = &mmi_sys_temp_fops,
};

static int mmi_sys_temp_get(struct thermal_zone_device *thermal,
			    int *temp)
{
	struct mmi_sys_temp_sensor *sensor = thermal->devdata;

	if (!sensor)
		return -EINVAL;

	*temp = sensor->temp;
	return 0;
}

static struct thermal_zone_device_ops mmi_sys_temp_ops = {
	.get_temp = mmi_sys_temp_get,
};

static void psy_changed_work_func(struct work_struct *work)
{
	int num_sensors_listener, i, desc = 0;
	char buf[1024];

	if (!sys_temp_dev)
		return;

	num_sensors_listener = sys_temp_dev->num_sensors_listener;

		for (i = 0; i < num_sensors_listener; i++) {
			if(sys_temp_dev->sensor_listener[i].tz_dev) {

				thermal_zone_get_temp(sys_temp_dev->sensor_listener[i].tz_dev,
						&sys_temp_dev->sensor_listener[i].temp);

				/*generate sensor uevent*/
				uevent_generate(sys_temp_dev, i);
			} else {
				dev_err(&sys_temp_dev->pdev->dev,
					"Invalid thermal zone\n");
				return;
			}
		}

		for (i = 0; i < num_sensors_listener; i++) {
			desc +=
				sprintf(buf + desc, "%s=%s%d.%d, ",
				sys_temp_dev->sensor_listener[i].name,
				sys_temp_dev->sensor_listener[i].temp < 0 ? "-" : "",
				abs(sys_temp_dev->sensor_listener[i].temp / 1000),
				abs(sys_temp_dev->sensor_listener[i].temp % 1000));
		}

		dev_info(&sys_temp_dev->pdev->dev,
				"%s\n", buf);
	return;
}

static int psy_changed(struct notifier_block *nb, unsigned long evt, void *ptr)
{
	if (!sys_temp_dev)
		return -EINVAL;

	if (evt == PSY_EVENT_PROP_CHANGED)
		schedule_work(&sys_temp_dev->psy_changed_work);

	return NOTIFY_OK;
}

static int mmi_sys_temp_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	struct device_node *node;
	int num_sensors;
	int num_registered = 0;
	int num_sensors_listener;

	if (!pdev)
		return -ENODEV;

	node = pdev->dev.of_node;
	if (!node) {
		dev_err(&pdev->dev, "bad of_node\n");
		return -ENODEV;
	}

	num_sensors = of_property_count_strings(node, TEMP_NODE_SENSOR_NAMES);
	if (num_sensors <= 0) {
		dev_err(&pdev->dev,
			"bad number of sensors: %d\n", num_sensors);
		return -EINVAL;
	}

	num_sensors_listener = of_property_count_strings(node, SENSOR_LISTENER_NAMES);
	if (num_sensors_listener <= 0) {
		dev_err(&pdev->dev,
			"bad number of sensors-listener: %d\n", num_sensors_listener);
	}
	sys_temp_dev = devm_kzalloc(&pdev->dev, sizeof(struct mmi_sys_temp_dev),
				    GFP_KERNEL);
	if (!sys_temp_dev) {
		dev_err(&pdev->dev,
			"Unable to alloc memory for sys_temp_dev\n");
		return -ENOMEM;
	}

	sys_temp_dev->pdev = pdev;
	sys_temp_dev->num_sensors = num_sensors;

	sys_temp_dev->sensor =
				(struct mmi_sys_temp_sensor *)devm_kzalloc(&pdev->dev,
				(num_sensors *
				       sizeof(struct mmi_sys_temp_sensor)),
				       GFP_KERNEL);
	if (!sys_temp_dev->sensor) {
		dev_err(&pdev->dev,
			"Unable to alloc memory for sensor\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_sensors; i++) {
		ret = of_property_read_string_index(node,
						TEMP_NODE_SENSOR_NAMES, i,
						&sys_temp_dev->sensor[i].name);
		if (ret) {
			dev_err(&pdev->dev, "Unable to read of_prop string\n");
			goto err_thermal_unreg;
		}

		sys_temp_dev->sensor[i].temp = DEFAULT_TEMPERATURE;
		sys_temp_dev->sensor[i].tz_dev =
		   thermal_zone_device_register(sys_temp_dev->sensor[i].name,
						0, 0,
						&sys_temp_dev->sensor[i],
						&mmi_sys_temp_ops,
						NULL, 0, 0);
		if (IS_ERR(sys_temp_dev->sensor[i].tz_dev)) {
			dev_err(&pdev->dev,
				"thermal_zone_device_register() failed.\n");
			ret = -ENODEV;
			goto err_thermal_unreg;
		}
		num_registered = i + 1;
	}

	/*uevent parameter process*/
	uevent_parse_dt(sys_temp_dev, node, num_sensors);

	platform_set_drvdata(pdev, sys_temp_dev);

	ret = misc_register(&mmi_sys_temp_misc);
	if (ret) {
		dev_err(&pdev->dev, "Error registering device %d\n", ret);
		goto err_thermal_unreg;
	}

	if (num_sensors_listener <= 0) {
		dev_info(&sys_temp_dev->pdev->dev,
				"No configure sensors listener !\n");
		goto err_sensors_listener;
	}

	sys_temp_dev->num_sensors_listener = num_sensors_listener;
	sys_temp_dev->sensor_listener =
				(struct mmi_sys_temp_sensor *)devm_kzalloc(&pdev->dev,
				(num_sensors_listener *
				       sizeof(struct mmi_sys_temp_sensor)),
				       GFP_KERNEL);
	if (!sys_temp_dev->sensor_listener) {
		dev_err(&pdev->dev,
			"Unable to alloc memory for sensor_listener\n");
		goto err_sensors_listener;
	}

	for (i = 0; i < num_sensors_listener; i++) {
		ret = of_property_read_string_index(node,
						SENSOR_LISTENER_NAMES, i,
						&sys_temp_dev->sensor_listener[i].name);
		if (ret) {
			dev_err(&pdev->dev, "Unable to read of_prop string\n");
			goto err_sensors_listener;
		}

		sys_temp_dev->sensor_listener[i].temp = DEFAULT_TEMPERATURE;
		if (sys_temp_dev->sensor_listener[i].name) {
			sys_temp_dev->sensor_listener[i].tz_dev =
			thermal_zone_get_zone_by_name(sys_temp_dev->sensor_listener[i].name);
			if (IS_ERR(sys_temp_dev->sensor_listener[i].tz_dev)) {
				dev_err(&pdev->dev,
					"thermal_zone_get_zone_by_name() failed."
					"name %s, i %d\n", sys_temp_dev->sensor_listener[i].name, i);
				goto err_sensors_listener;
			}
		} else {
			dev_err(&pdev->dev,
				"Invalid sensor listener name\n");
			goto err_sensors_listener;
		}
	}

	INIT_WORK(&sys_temp_dev->psy_changed_work, psy_changed_work_func);
	sys_temp_dev->psy_nb.notifier_call = psy_changed;
	power_supply_reg_notifier(&sys_temp_dev->psy_nb);
err_sensors_listener:
	return 0;

err_thermal_unreg:
	for (i = 0; i < num_registered; i++)
		thermal_zone_device_unregister(sys_temp_dev->sensor[i].tz_dev);

	devm_kfree(&pdev->dev, sys_temp_dev);
	return ret;
}

static int mmi_sys_temp_remove(struct platform_device *pdev)
{
	int i;
	struct mmi_sys_temp_dev *dev =  platform_get_drvdata(pdev);

	for (i = 0; i < dev->num_sensors; i++)
		thermal_zone_device_unregister(dev->sensor[i].tz_dev);

	misc_deregister(&mmi_sys_temp_misc);
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, sys_temp_dev);
	return 0;
}

static const struct of_device_id mmi_sys_temp_match_table[] = {
	{.compatible = "mmi,sys-temp"},
	{},
};
MODULE_DEVICE_TABLE(of, mmi_sys_temp_match_table);

static struct platform_driver mmi_sys_temp_driver = {
	.probe = mmi_sys_temp_probe,
	.remove = mmi_sys_temp_remove,
	.driver = {
		.name = "mmi_sys_temp",
		.owner = THIS_MODULE,
		.of_match_table = mmi_sys_temp_match_table,
	},
};

static int __init mmi_sys_temp_init(void)
{
	return platform_driver_register(&mmi_sys_temp_driver);
}

static void __exit mmi_sys_temp_exit(void)
{
	platform_driver_unregister(&mmi_sys_temp_driver);
}

module_init(mmi_sys_temp_init);
module_exit(mmi_sys_temp_exit);

MODULE_ALIAS("platform:mmi_sys_temp");
MODULE_AUTHOR("Motorola Mobility LLC");
MODULE_DESCRIPTION("Motorola Mobility System Temperatures");
MODULE_LICENSE("GPL");
