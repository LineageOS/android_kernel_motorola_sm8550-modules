/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"msm-dsi-display:[%s] " fmt, __func__

#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/random.h>

#include <linux/string.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/firmware.h>

#include "msm_drv.h"
#include "sde_connector.h"
#include "msm_mmu.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"
#include "dsi_parser.h"
#include "dsi_display_mot_ext.h"

//Timer for pressure test and debug
static struct alarm *g_wakeup_timer = NULL;
static int g_wakeup_timer_interval = 0;
static int g_pressure_test_en = 0;

static int g_param_id_flag = 0;  //Decimal:  PanelParmId=g_param_id_flag/100   DisplaModeCmdId=g_param_id_flag%100

extern const char *cmd_set_prop_map[DSI_CMD_SET_MAX];

static enum alarmtimer_restart dsi_display_wakeup_timer_func(struct alarm *alarm, ktime_t now)
{
	ktime_t start, add;
	int randomTime = 0;
	pr_info("g_wakeup_timer_interval %d, g_pressure_test_en %d\n", g_wakeup_timer_interval, g_pressure_test_en);
	//here add code to do the pressure test for timer wakeup

	if (g_wakeup_timer_interval != 0) {
		//srand(time(NULL));
		start = ktime_get_boottime();
		randomTime = prandom_u32_max(g_wakeup_timer_interval) + g_wakeup_timer_interval;
		add = ktime_set(randomTime, 0);
		alarm_start(g_wakeup_timer, ktime_add(start, add));
		pr_info("%s: randomTimer %d seconds set\n", __func__, randomTime);
	}

	return ALARMTIMER_NORESTART;
}

static int my_atoi(const char *src)
{
      int s = 0;
      bool isMinus = false;

      while(*src == ' ')
      {
          src++;
      }

      if(*src == '+' || *src == '-')
      {
          if(*src == '-')
          {
              isMinus = true;
          }
          src++;
      }
      else if(*src < '0' || *src > '9')
      {
          s = 2147483647;
          return s;
      }

      while(*src != '\0' && *src >= '0' && *src <= '9')
      {
          s = s * 10 + *src - '0';
          src++;
      }
      return s * (isMinus ? -1 : 1);
}

static u32 atoh(const unsigned char *in)
{
	u32 sum = 0;
	unsigned int mult = 1;
	unsigned char c;
	unsigned int len = strlen(in);

	while (len) {
		int value;

		c = in[len - 1];
		value = hex_to_bin(c);
		if (value >= 0)
			sum += mult * value;
		mult *= 16;
		--len;
	}
	return sum;
}

static char *strtok(char *s, const char * sep)
{
	static char *p;
	if (!s && !(s = p)) return NULL;
	s += strspn(s, sep);
	if (!*s) return p = 0;
	p = s + strcspn(s, sep);
	if (*p) *p++ = 0;
	else p = 0;
	return s;
}

static bool isNumStr(char *s)
{
	int i = 0;
	int len;
	char* pbuf = s;

	len = strlen(pbuf);
	while(i < len) {
		if(!((pbuf[i]>= '0' && pbuf[i] <= '9' ) || (pbuf[i] >= 'a' && pbuf[i] <= 'f' ) || (pbuf[i] >= 'A' && pbuf[i] <= 'F' )))
			return false;
		i++;
	}
	return true;
}

static char* dsi_panel_line_remove_front_space(char* pinstr)
{
	char* pbuf = NULL;
	int i = 0;
	int len = strlen(pinstr);
	pbuf = pinstr;
	while(i < len) {
		if( pbuf[i]  !=  0x20 && pbuf[i]  !=  0x09 )  //skip space and horizontal tab
			break;
		i++;
	}
	//pr_info("strlen %d\n", len);
	return pbuf + i;
}

static int dsi_panel_get_cmd_pkt_count_mot(const char *data, u32 length, u32 *cnt)
{
	const u32 cmd_set_min_size = 7;
	u32 count = 0;
	u32 packet_length;
	u32 tmp;
	char* pbuf = NULL;
	char tembuf[8];
	int i;

	pbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	while (length >= cmd_set_min_size) {
		packet_length = cmd_set_min_size;
		tmp = ((data[5] << 8) | (data[6]));
		packet_length += tmp;
		if (packet_length > length) {
			pr_err("format error\n");
			return -EINVAL;
		}
#if 1 //print data
		if (pbuf) {
			snprintf(pbuf, PAGE_SIZE, "packet_data:  %2x %2x %2x %2x %2x  %2x  %2x  ",
				data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
		}
		for (i=0; i<tmp; i++) {
			memset(tembuf, 0, 8);
			snprintf(tembuf, 8, " %2x", data[cmd_set_min_size+i]);
			strcat(pbuf, tembuf);
		}
		strcat(pbuf, "\n");
		printk("%s", pbuf);
#endif
		length -= packet_length;
		data += packet_length;
		count++;
	};

	*cnt = count;
	if (pbuf)
		kfree(pbuf);
	return 0;
}

static void dsi_panel_destroy_cmd_packets_mot(struct dsi_panel_cmd_set *set)
{
	u32 i = 0;
	struct dsi_cmd_desc *cmd;

	for (i = 0; i < set->count; i++) {
		cmd = &set->cmds[i];
		kfree(cmd->msg.tx_buf);
	}
}

static void dsi_panel_dealloc_cmd_packets_mot(struct dsi_panel_cmd_set *set)
{
	kfree(set->cmds);
}

static int dsi_panel_create_cmd_packets_mot(const char *data,
					u32 length,
					u32 count,
					struct dsi_cmd_desc *cmd)
{
	int rc = 0;
	int i, j;
	u8 *payload;

	for (i = 0; i < count; i++) {
		u32 size;

		cmd[i].msg.type = data[0];
		cmd[i].msg.channel = data[2];
		cmd[i].msg.flags |= data[3];
		cmd[i].ctrl = 0;
		cmd[i].post_wait_ms = data[4];
		cmd[i].msg.tx_len = ((data[5] << 8) | (data[6]));

		if (cmd[i].msg.flags & MIPI_DSI_MSG_BATCH_COMMAND)
			cmd[i].last_command = false;
		else
			cmd[i].last_command = true;

		size = cmd[i].msg.tx_len * sizeof(u8);

		payload = kzalloc(size, GFP_KERNEL);
		if (!payload) {
			rc = -ENOMEM;
			goto error_free_payloads;
		}

		for (j = 0; j < cmd[i].msg.tx_len; j++)
			payload[j] = data[7 + j];

		cmd[i].msg.tx_buf = payload;
		data += (7 + cmd[i].msg.tx_len);
	}

	return rc;
error_free_payloads:
	for (i = i - 1; i >= 0; i--) {
		cmd--;
		kfree(cmd->msg.tx_buf);
	}

	return rc;
}

static int dsi_panel_alloc_cmd_packets_mot(struct dsi_panel_cmd_set *cmd,
					u32 packet_count)
{
	u32 size;

	size = packet_count * sizeof(*cmd->cmds);
	cmd->cmds = kzalloc(size, GFP_KERNEL);
	if (!cmd->cmds)
		return -ENOMEM;

	cmd->count = packet_count;
	return 0;
}

static bool dsi_panel_param_str_append(char* sDest, char* sSrc, int destSize)
{
	bool ret = true;
	if ( (strlen(sDest) + strlen(sSrc))  < (destSize-7)) {
	    //pr_info("%s append, dlen %d, slen %d, tsize %d\n", __func__, strlen(sDest), strlen(sSrc), destSize);
	    strcat(sDest, sSrc);
	} else if (destSize > (strlen(sDest)+7)) {
	    //pr_info("%s cut, dlen %d, slen %d, tsize %d, cpsize %d\n", __func__, strlen(sDest), strlen(sSrc), destSize, destSize-strlen(sDest)-7);
	    strncpy(sDest+strlen(sDest), sSrc, destSize-strlen(sDest)-7);
	} else {
	    //pr_info("%s overflow, dlen %d, slen %d, tsize %d\n", __func__, strlen(sDest), strlen(sSrc), destSize);
	    strncpy(sDest+destSize-7, "......", 6);
	    ret = false;
	}
	return ret;
}

static bool dsi_panel_mot_parse_commands(char* str, u32* length, struct dsi_display_mode *display_mode, enum dsi_cmd_set_type type, char* pcur)
{
	char * pos = NULL;
	char * posStart = NULL;
	char * posEnd = NULL;
	char *buf = NULL;  //dont change this point after alloc, else kfree will error and kernel panic
	char *pbuf = NULL;
	char *pLine = NULL;
	char *pLineEnd = NULL;
	char *token = NULL;
	char numstr[8];
	uint len = 0;
	uint cmdlen = 0;
	u8* data = NULL;
	uint index = 0;
	char delim[] = " \t";  //space and horizontal tab
	struct dsi_mode_info *mode_timing = NULL ;
	struct dsi_display_mode_priv_info *priv_info;

	mode_timing = &display_mode->timing;
	priv_info = display_mode->priv_info;
	pr_info("panel h_active:%d HFP:%d HBP:%d HPW:%d h_skew:%d,   v_active:%d VFP:%d VBP:%d VPW:%d,   panel clk rate:%d mdp_transfer_time_us:%d refresh_rate:%d\n",
	            mode_timing->h_active, mode_timing->h_front_porch, mode_timing->h_back_porch, mode_timing->h_sync_width, mode_timing->h_skew,
	            mode_timing->v_active, mode_timing->v_front_porch, mode_timing->v_back_porch, mode_timing->v_sync_width,
	            priv_info->clk_rate_hz , priv_info->mdp_transfer_time_us, mode_timing->refresh_rate);

	//pr_info("input str : %s\n", str);
	*length = 0;
	pos = strstr(str, "=");
	if (pos) {
		posStart = strstr(pos, "[");
		if (posStart) {
			posStart ++;
			posEnd = strstr(posStart, "]");
			if (posEnd) {
				pos = strstr(posEnd, "\n");
				if (pos)
					posEnd = pos;
				*length = posEnd - pcur + 1;
			}
			if (posEnd) {
				pr_info("posStart address(%llx), val: posStart[0][1][2]=[%2x][%2x][%2x],   posEnd address(%llx), val: posEnd[0][1][2]=[%2x][%2x][%2x]\n",
					posStart, posStart[0], posStart[1], posStart[2], posEnd, posEnd[0], posEnd[1], posEnd[2]);
				len = posEnd - posStart - 1;
				//*length = len - 1;
				buf = kmalloc(len+5, GFP_KERNEL);	//data in this buffer can be modified, if there is "\n", we also copy it to buf
				if (!buf) {
					pr_warn("kmalloc failed for command buf\n");
					goto error_exit;
				}
				pbuf = buf;
				memset(pbuf, 0, len+5);
				//strncpy(pbuf, posStart, len);
				memcpy(pbuf, posStart, len);
				//copy_from_user(pbuf, posStart, len);
				//pbuf[len] = '\0';
				data = (u8*)pbuf;
				posEnd = pbuf + len;
				memset(numstr, 0, 8);
				pr_info("bufStart address(%llx), val: bufStart[0][1][2]=[%2x][%2x][%2x],   bufEnd address(%llx), val: bufEnd[0][1][2]=[%2x][%2x][%2x], len=%d\n",
					pbuf, pbuf[0], pbuf[1], pbuf[2], posEnd, posEnd[0], posEnd[1], posEnd[2], len);
				pLine = pbuf;
				pLineEnd = posEnd;
				token = pLine;

				while (pbuf < posEnd) {

					pLineEnd = strstr(pLine, "\n");
					if (pLineEnd) {
						pLineEnd--;  //If this is a '\n' only line, ptr need --
					} else
						pLineEnd = posEnd;

					pLineEnd[0] = '\0'; //add terminal flag and remove '\n' in the str
					pLineEnd[1] = '\0';
					token = strstr(pLine, "]");
					if (!token) {
						token = strstr(pLine, "//");
						if (!token)
							token = strstr(pLine, "/*");
					}
					if (token) {
						//pr_info("there is comment '/*' in pLine address: %llx, token[0]=%c, pLine str: %s\n", pLine, token[0], pLine);
						token[0] = '\0';
					} else {
						token = pLineEnd;
					}
					//pr_info("#pre pLine address: %llx, pLine[0]=%x, pLine str: %s\n", pLine, pLine[0], pLine);
					pLine = dsi_panel_line_remove_front_space(pLine);
					pr_info("=pst pLine address: %llx, pLine[0]=%x, pLine str: %s\n", pLine, pLine[0], pLine);
					//pr_info("token address:     %llx,      token[0][1][2]=[%2x][%2x][%2x]\n", token, token[0], token[1], token[2]);
					//pr_info("pLineEnd address: %llx, pLineEnd[0][1][2]=[%2x][%2x][%2x]\n", pLineEnd, pLineEnd[0], pLineEnd[1], pLineEnd[2]);
					pLineEnd += 2;
					if (pLine != token) {
						for(token = strtok(pLine, delim); token != NULL; token = strtok(NULL, delim)) {
							if (isNumStr(token)) {
								data[index++] = atoh(token);
								//pr_info("find num str: %s -> %2x\n", token, data[index-1]);
							}
						}
					} else
						pr_info("nothing line: %llx, pLine str: %s\n", pLine, pLine);

					pbuf = pLineEnd ;
					pLine = pLineEnd;

				}
				if (display_mode->timing.refresh_rate == 144)
					msleep(256);

				dsi_panel_get_cmd_pkt_count_mot(data, index, &cmdlen);
				if (display_mode->timing.refresh_rate == 144)
					msleep(256);
				pr_info("got %d command packets for refresh rate %d\n", cmdlen, display_mode->timing.refresh_rate);
				if (type < DSI_CMD_SET_MAX && display_mode->timing.refresh_rate != 144) {
					dsi_panel_destroy_cmd_packets_mot(&display_mode->priv_info->cmd_sets[type]);
					dsi_panel_dealloc_cmd_packets_mot(&display_mode->priv_info->cmd_sets[type]);
					dsi_panel_alloc_cmd_packets_mot(&display_mode->priv_info->cmd_sets[type], cmdlen);
					dsi_panel_create_cmd_packets_mot(data, 0, cmdlen, display_mode->priv_info->cmd_sets[type].cmds);
				}

			} else {
				pr_err(" ] not found in this line!\n");
				goto error_exit;
			}
		} else {
			pr_err(" [ not found in this line\n");
			goto error_exit;
		}
	} else {
		pr_err(" = not found in this line!\n");
		goto error_exit;
	}

error_exit:
	if (buf)
		kfree(buf);
	//pr_info("need skip length : %d\n", *length);
	return true;
}


static bool dsi_panel_mot_parse_param_commands(char* str, u32* length, struct dsi_display *display, enum msm_param_id parmId, enum dsi_cmd_set_type type, char* pcur)
{
	char * pos = NULL;
	char * posStart = NULL;
	char * posEnd = NULL;
	char *buf = NULL;  //dont change this point after alloc, else kfree will error and kernel panic
	char *pbuf = NULL;
	char *pLine = NULL;
	char *pLineEnd = NULL;
	char *token = NULL;
	char numstr[8];
	uint len = 0;
	uint cmdlen = 0;
	u8* data = NULL;
	uint index = 0;
	char delim[] = " \t";  //space and horizontal tab
	struct panel_param *param;
	struct panel_param_val_map *param_map;
	struct dsi_panel_cmd_set *cmd_set = NULL;
	int j;

	if (!display || !display->panel)
		return -EINVAL;

	param = &display->panel->param_cmds[parmId];
	if (!param)
		return -EINVAL;
	for (j = 0; j < param->val_max && param->val_max > 0 ; j++) {
		param_map = &param->val_map[j];
		if(type == param_map->type)
			cmd_set = param_map->cmds;
	}
	if (!cmd_set)
		return -EINVAL;


	//pr_info("input str : %s\n", str);
	*length = 0;
	pos = strstr(str, "=");
	if (pos) {
		posStart = strstr(pos, "[");
		if (posStart) {
			posStart ++;
			posEnd = strstr(posStart, "]");
			if (posEnd) {
				pos = strstr(posEnd, "\n");
				if (pos)
					posEnd = pos;
				*length = posEnd - pcur + 1;
			}
			if (posEnd) {
				pr_info("posStart address(%llx), val: posStart[0][1][2]=[%2x][%2x][%2x],   posEnd address(%llx), val: posEnd[0][1][2]=[%2x][%2x][%2x]\n",
					posStart, posStart[0], posStart[1], posStart[2], posEnd, posEnd[0], posEnd[1], posEnd[2]);
				len = posEnd - posStart - 1;
				//*length = len - 1;
				buf = kmalloc(len+5, GFP_KERNEL);	//data in this buffer can be modified, if there is "\n", we also copy it to buf
				if (!buf) {
					pr_warn("kmalloc failed for command buf\n");
					goto error_exit;
				}
				pbuf = buf;
				memset(pbuf, 0, len+5);
				//strncpy(pbuf, posStart, len);
				memcpy(pbuf, posStart, len);
				//copy_from_user(pbuf, posStart, len);
				//pbuf[len] = '\0';
				data = (u8*)pbuf;
				posEnd = pbuf + len;
				memset(numstr, 0, 8);
				pr_info("bufStart address(%llx), val: bufStart[0][1][2]=[%2x][%2x][%2x],   bufEnd address(%llx), val: bufEnd[0][1][2]=[%2x][%2x][%2x], len=%d\n",
					pbuf, pbuf[0], pbuf[1], pbuf[2], posEnd, posEnd[0], posEnd[1], posEnd[2], len);
				pLine = pbuf;
				pLineEnd = posEnd;
				token = pLine;

				while (pbuf < posEnd) {

					pLineEnd = strstr(pLine, "\n");
					if (pLineEnd) {
						pLineEnd--;  //If this is a '\n' only line, ptr need --
					} else
						pLineEnd = posEnd;

					pLineEnd[0] = '\0'; //add terminal flag and remove '\n' in the str
					pLineEnd[1] = '\0';
					token = strstr(pLine, "]");
					if (!token) {
						token = strstr(pLine, "//");
						if (!token)
							token = strstr(pLine, "/*");
					}
					if (token) {
						//pr_info("there is comment '/*' in pLine address: %llx, token[0]=%c, pLine str: %s\n", pLine, token[0], pLine);
						token[0] = '\0';
					} else {
						token = pLineEnd;
					}
					//pr_info("#pre pLine address: %llx, pLine[0]=%x, pLine str: %s\n", pLine, pLine[0], pLine);
					pLine = dsi_panel_line_remove_front_space(pLine);
					pr_info("=pst pLine address: %llx, pLine[0]=%x, pLine str: %s\n", pLine, pLine[0], pLine);
					//pr_info("token address:     %llx,      token[0][1][2]=[%2x][%2x][%2x]\n", token, token[0], token[1], token[2]);
					//pr_info("pLineEnd address: %llx, pLineEnd[0][1][2]=[%2x][%2x][%2x]\n", pLineEnd, pLineEnd[0], pLineEnd[1], pLineEnd[2]);
					pLineEnd += 2;
					if (pLine != token) {
						for(token = strtok(pLine, delim); token != NULL; token = strtok(NULL, delim)) {
							if (isNumStr(token)) {
								data[index++] = atoh(token);
								//pr_info("find num str: %s -> %2x\n", token, data[index-1]);
							}
						}
					} else
						pr_info("nothing line: %llx, pLine str: %s\n", pLine, pLine);

					pbuf = pLineEnd ;
					pLine = pLineEnd;

				}

				dsi_panel_get_cmd_pkt_count_mot(data, index, &cmdlen);
				pr_info("got %d command packets for %s, type %d\n", cmdlen, param->param_name, type);
				if (type < DSI_CMD_SET_MAX) {
					dsi_panel_destroy_cmd_packets_mot(cmd_set);
					dsi_panel_dealloc_cmd_packets_mot(cmd_set);
					dsi_panel_alloc_cmd_packets_mot(cmd_set, cmdlen);
					dsi_panel_create_cmd_packets_mot(data, 0, cmdlen, cmd_set->cmds);
				}

			} else {
				pr_err(" ] not found in this line!\n");
				goto error_exit;
			}
		} else {
			pr_err(" [ not found in this line\n");
			goto error_exit;
		}
	} else {
		pr_err(" = not found in this line!\n");
		goto error_exit;
	}

error_exit:
	if (buf)
		kfree(buf);
	//pr_info("need skip length : %d\n", *length);
	return true;
}

static bool dsi_panel_mot_parse_u32(char* str, const char* keystr, u32* val)
{
	char * pos = NULL;
	char * posStart = NULL;
	char * posEnd = NULL;
	char* pContent = NULL;
	char buf[64];
	uint len = 0;
	u32 keylen;

	keylen = strlen(keystr);
	if ( strncmp(str, keystr, keylen) )
		return false;

	pContent = str + keylen;
	posStart = strstr(pContent, "=");
	if (posStart) {
		posStart = strstr(posStart, "<");
		if (posStart) {
			posStart ++;
			posEnd = strstr(posStart, ">");
			if (posEnd) {
				pos = strstr(posStart, " ");
				while(pos != NULL) {
					if (pos == posStart)
						posStart++;
					else if (pos > posStart) {
						posEnd = pos;
						break;
					}
					pos = strstr(posStart, " ");
				}
				len = posEnd - posStart;
				strncpy(buf, posStart, len);
				buf[len] = 0;
				//pr_info("val str : %s\n", buf);
				*val = my_atoi(buf);
			} else {
				pr_err(" > not found in this line!\n");
				goto error_exit;
			}
		} else {
			pr_err(" < not found in this line, please try find [\n");
			goto error_exit;
		}
	} else {
		pr_err(" = not found in this line!\n");
		goto error_exit;
	}
	return true;

error_exit:
	return false;
}

#define PARAM_READ_FILE 0
bool dsi_panel_mot_parse_timing_from_file(struct dsi_display *display, int index)
{
	bool ret = false;
	int rc = 0;
	u32 val = 0;
	u32 keylen, llen, cmdstrlen;
	struct dsi_display_mode *display_mode;
	char keystr[128];
	char * fwbuf = NULL;
	char * pbuf = NULL;
	char * pline = NULL;
	char * plinebuf = NULL;
	char * plineEnd = NULL;
	const struct firmware *fw = NULL;
	int rate_cont = 0;
	char file_path[255] = { 0 };
#if PARAM_READ_FILE
	struct file *filp = NULL;
	struct inode *inode;
	mm_segment_t old_fs;
	loff_t pos;
	loff_t file_len = 0;
#endif

	if (!display) {
		pr_warn("display is null\n");
		return ret;
	}

#if PARAM_READ_FILE

	snprintf(file_path, 255, "/data/vendor/param/display/%s.txt", display->panel->name);
	filp = filp_open(file_path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		pr_err("%s: failed to open file %s\n", __func__, file_path);
		return ret;
	}

	inode = filp->f_inode;
	file_len = inode->i_size;
	fwbuf = kmalloc(file_len+1, GFP_KERNEL);
	if (!fwbuf) {
		pr_err("%s: kmalloc failed for file %s, file_len: %d\n", __func__, file_path, file_len);
		filp_close(filp, NULL);
		return ret;
	}
	memset(fwbuf, 0, file_len+1);
	pr_info("%s: open file %s success, length: %d\n", __func__, file_path, file_len);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	rc = kernel_read(filp, fwbuf, file_len , &pos);
	pr_info("%s: read got %d bytes, pos:%d, panel has %d timing modes\n", __func__, rc, (u32)pos, display->panel->num_display_modes);
	filp_close(filp, NULL);
	set_fs(old_fs);
       // just for compatible to requesting firmware
	fw = kzalloc(sizeof(struct firmware), GFP_KERNEL);
       fw->size = file_len;
       fw->data = fwbuf;
#else

	snprintf(file_path, 255, "%s.txt", display->panel->name);
	rc = request_firmware(&fw, file_path, &display->pdev->dev);//
	if (rc < 0) {
		dev_warn_once(&display->pdev->dev, "Request firmware failed - /data/vendor/param/firmware/%s (%d)\n", file_path, rc);
		return ret;
	}
	pr_info("found /data/vendor/param/firmware/%s, size=%d, with %d timing modes\n", file_path, fw->size, display->panel->num_display_modes);

	if (fw->size < 8 || fw->size > 65536) {
		dev_warn_once(&display->pdev->dev, "Invalid firmware size (%zu)\n", fw->size);
	}

	fwbuf = kmalloc(fw->size+1, GFP_KERNEL);
	if (!fwbuf) {
		pr_err("kmalloc failed for fwbuf\n");
		release_firmware(fw);
		return ret;
	}
	memset(fwbuf, 0, fw->size + 1);
	memcpy(fwbuf, fw->data, fw->size);

#endif

	pbuf = fwbuf;

	plinebuf = kmalloc(LCD_PARA_LINE_LEN, GFP_KERNEL);
	memset(plinebuf, 0, LCD_PARA_LINE_LEN);
	if (!plinebuf) {
		pr_warn("kmalloc failed for pline\n");
		goto timing_err_exit;
	}

	plineEnd = strstr(pbuf, "\n");
	if (plineEnd) {
		llen = plineEnd - pbuf;
		if (llen < LCD_PARA_LINE_LEN) {
			//strncpy(plinebuf, pbuf, llen);
			memcpy(plinebuf, pbuf, llen);
			plinebuf[llen] = '\0';
			pr_info("First line pre:  pbuf address(%llx), len%d, plinebuf[0]=%2x, plinebuf str: %s\n", pbuf, llen, plinebuf[0], plinebuf);
			pline = dsi_panel_line_remove_front_space(plinebuf);
			if (pline >= plinebuf)
				llen -= (pline - plinebuf);
			else {
				pr_warn("error line a\n");
				goto timing_err_exit;
			}
			pr_info("First line pst:  pbuf address(%llx), len%d, pline[0]=%2x, pline str: %s\n", pbuf, llen, pline[0], pline);
			pbuf = plineEnd + 1;
		} else {
			pr_warn("too large line a\n");
			goto timing_err_exit;
		}
	} else {
		pr_warn("found no line\n");
		goto timing_err_exit;
	}

	while (pbuf < (fwbuf + fw->size)) {

		if ( strncmp(pline, "//", 2)  == 0) {
			//go relaign pbuf
			goto goContinue;
		}
		if ( strncmp(pline, "/*", 2)  == 0) {
			//go relaign pbuf
			goto goContinue;
		}

		if (dsi_panel_mot_parse_u32(pline, "ParamVersion", &val)) {
			pr_info("got ParamVersion: %d\n", val);
			if (val > display->panel->paramVersion) {
			    goto timing_err_exit;
			}
		}


		for(rate_cont = 0; rate_cont < display->panel->num_display_modes; rate_cont ++) {
			display_mode = &display->modes[rate_cont];//container_of(mode_timing, struct dsi_display_mode, timing);

			// qcom,mdss-dsi-on-command
			memset(keystr, 0, 128);
			snprintf(keystr, 128, "qcom,mdss-dsi-on-command@%d", rate_cont);
			keylen = strlen(keystr);
			if ( strncmp(pline, keystr, keylen)  == 0) {
			         pr_info("Start parse commands for %s #####\n", keystr);
			         if (dsi_panel_mot_parse_commands(pbuf - llen + keylen -1, &cmdstrlen, display_mode, DSI_CMD_SET_ON, pbuf)) {
			                 pr_info("got commands of %s: cmdstrlen: %d\n", keystr, cmdstrlen);
			                 pbuf += cmdstrlen;
			         }
			}

			// qcom,mdss-dsi-off-command
			memset(keystr, 0, 128);
			snprintf(keystr, 128, "qcom,mdss-dsi-off-command@%d", rate_cont);
			keylen = strlen(keystr);
			if ( strncmp(pline, keystr, keylen)  == 0) {
				pr_info("Start parse commands for %s #####\\n", keystr);
				if (dsi_panel_mot_parse_commands(pbuf - llen + keylen -1, &cmdstrlen, display_mode, DSI_CMD_SET_OFF, pbuf)) {
			             pr_info("got commands of %s: cmdstrlen: %d\n", keystr, cmdstrlen);
					pbuf += cmdstrlen;
				}
			}

			// qcom,mdss-dsi-timing-switch-command
			memset(keystr, 0, 128);
			snprintf(keystr, 128, "qcom,mdss-dsi-timing-switch-command@%d", rate_cont);
			keylen = strlen(keystr);
			if ( strncmp(pline, keystr, keylen)  == 0) {
				pr_info("Start parse commands for %s #####\\n", keystr);
				if (dsi_panel_mot_parse_commands(pbuf - llen + keylen -1, &cmdstrlen, display_mode, DSI_CMD_SET_TIMING_SWITCH, pbuf)) {
			             pr_info("got commands of %s: cmdstrlen: %d\n", keystr, cmdstrlen);
					pbuf += cmdstrlen;
				}
			}
		}

		// qcom,mdss-dsi-dc-on-command
		memset(keystr, 0, 128);
		snprintf(keystr, 128, "qcom,mdss-dsi-dc-on-command");
		keylen = strlen(keystr);
		if ( strncmp(pline, keystr, keylen)  == 0) {
			pr_info("Start parse commands for %s #####\\n", keystr);
			if (dsi_panel_mot_parse_param_commands(pbuf - llen + keylen -1, &cmdstrlen, display, PARAM_DC_ID, DSI_CMD_SET_DC_ON, pbuf)) {
					 pr_info("got commands of %s: cmdstrlen: %d\n", keystr, cmdstrlen);
				pbuf += cmdstrlen;
			}
		}

		// qcom,mdss-dsi-dc-off-command
		memset(keystr, 0, 128);
		snprintf(keystr, 128, "qcom,mdss-dsi-dc-off-command");
		keylen = strlen(keystr);
		if ( strncmp(pline, keystr, keylen)  == 0) {
			pr_info("Start parse commands for %s #####\\n", keystr);
			if (dsi_panel_mot_parse_param_commands(pbuf - llen + keylen -1, &cmdstrlen, display, PARAM_DC_ID, DSI_CMD_SET_DC_OFF, pbuf)) {
					 pr_info("got commands of %s: cmdstrlen: %d\n", keystr, cmdstrlen);
				pbuf += cmdstrlen;
			}
		}


goContinue:
		memset(plinebuf, 0, LCD_PARA_LINE_LEN);
		plineEnd = strstr(pbuf, "\n");
		if (plineEnd) {
			llen = plineEnd - pbuf;
			if (llen < LCD_PARA_LINE_LEN) {
				//strncpy(plinebuf, pbuf, llen);
				memcpy(plinebuf, pbuf, llen);
				plinebuf[llen] = '\0';
				//pr_info("New line pre:  pbuf address(%llx), len%d, plinebuf[0]=%2x, plinebuf str: %s\n", pbuf, llen, plinebuf[0], plinebuf);
				pline = dsi_panel_line_remove_front_space(plinebuf);
				if (pline >= plinebuf)
					llen -= (pline - plinebuf);
				else {
					pr_warn("error line b\n");
					goto timing_err_exit;
				}
				if (llen > 7) {
					//pr_info("New line pst:  pbuf(%llx),  pbuf[0][1][2][3][4][5][6][7]=[%2x][%2x][%2x][%2x][%2x][%2x][%2x][%2x], len %d, pline[0]=%2x, pline str: %s\n",
					//	pbuf, pbuf[0], pbuf[1], pbuf[2], pbuf[3], pbuf[4], pbuf[5], pbuf[6], pbuf[7], llen, pline[0], pline);
					pr_info("New line pst:  pbuf(%llx), len %d, pline[0]=%2x, pline str: %s\n", pbuf, llen, pline[0], pline);
				}
				pbuf = plineEnd + 1;
			} else {
				pr_warn("too large line b\n");
				goto timing_err_exit;
			}
		} else {
			llen = fwbuf + fw->size - pbuf;
			memcpy(plinebuf, pbuf, llen);
			plinebuf[llen] = '\0';
			pr_info("End line:  pbuf(%llx): len%d:  %s\n", pbuf, llen, plinebuf);
			break;
		}

	}

	ret = true;

timing_err_exit:
	if (plinebuf)
		kfree(plinebuf);
	if (fwbuf)
		kfree(fwbuf);
#if PARAM_READ_FILE
	if (fw)
		kfree(fw);
#else
	release_firmware(fw);
#endif
	return ret;
}

static ssize_t dsi_display_wakup_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int rc = 0;

	rc = snprintf(buf, PAGE_SIZE, "%s: timerhandle:%p\n",  (g_wakeup_timer_interval==0)? "Stopped":"Started", g_wakeup_timer);
	pr_info("%s:  %s: timerhandle:%p\n", __func__,  (g_wakeup_timer_interval==0)? "Stopped":"Started", g_wakeup_timer);

	return rc;
}

static ssize_t dsi_display_wakup_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *conn;
	struct sde_connector *sde_conn;
	struct dsi_display *display;
	ktime_t now, add;
	u16 val = 0;

	conn = dev_get_drvdata(dev);
	sde_conn = to_sde_connector(conn);
	display = sde_conn->display;
	if (!display) {
		pr_err("Invalid  input: display\n");
		return count;
	}

	if (kstrtou16(buf, 16, &val) < 0)
		return count;
	g_wakeup_timer_interval = val&0x7fff;

	pr_info("val: 0x%x(enable:0x%x : interval:%d seconds)\n", val, (val&0x8000), g_wakeup_timer_interval);

	if (g_wakeup_timer == NULL) {
		g_wakeup_timer = devm_kzalloc(&display->pdev->dev, sizeof(*g_wakeup_timer), GFP_KERNEL);
		if (g_wakeup_timer) {
			alarm_init(g_wakeup_timer, ALARM_BOOTTIME, dsi_display_wakeup_timer_func);
		}
		else {
			pr_err("failed to init timer\n");
			return count;
		}
	}

	// 0xffff :   0x8000/enable/disable    0x7fff/interval seconds
	if (val&0x8000) {
		now = ktime_get_boottime();
		add = ktime_set(g_wakeup_timer_interval, 0);
		alarm_start(g_wakeup_timer, ktime_add(now, add));
	}else {
		if (g_wakeup_timer)
			alarm_cancel(g_wakeup_timer);
		g_wakeup_timer_interval = 0;
	}
	return count;
}

static ssize_t dsi_display_pressure_test_en_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int rc = 0;

	rc = snprintf(buf, PAGE_SIZE, "g_pressure_test_en: %d\n",   g_pressure_test_en);
	pr_info("g_pressure_test_en:%d\n", g_pressure_test_en);

	return rc;
}

static ssize_t dsi_display_pressure_test_en_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *conn;
	struct sde_connector *sde_conn;
	struct dsi_display *display;
	u16 val = 0;

	if (!dev || !buf) {
		pr_err("%s: Invalid input: dev(%s), buf(%s)\n", __func__, dev? "valid" : "null", buf? "valid" : "null");
		return count;
	}

	conn = dev_get_drvdata(dev);
	sde_conn = to_sde_connector(conn);
	display = sde_conn->display;
	if (!display) {
		pr_err("Invalid  input: display\n");
		return count;
	}

	if (kstrtou16(buf, 10, &val) < 0)
		return count;
	g_pressure_test_en = val;

	pr_info("interval:%d seconds, g_pressure_test_en %d\n",
		g_wakeup_timer_interval, g_pressure_test_en);

	return count;
}

static void dsi_display_show_para(char* buf, char* pbuf, enum dsi_cmd_set_type type, struct dsi_display_mode_priv_info *priv_info)
{
	int i,j,rc;
	u8* data;
	int count = priv_info->cmd_sets[type].count;
	char ptembuf[LCD_PARA_TEM_BUF_LEN];
	char pLineBuf[LCD_PARA_LINE_LEN];

	memset(pLineBuf, 0, LCD_PARA_LINE_LEN);
	rc = snprintf(pLineBuf, LCD_PARA_LINE_LEN, "#[%s]#: count:%d\n", cmd_set_prop_map[type], count);
	dsi_panel_param_str_append(pbuf, pLineBuf, PAGE_SIZE*2);
	pr_info("%s\n", pLineBuf);
	//strcat(pbuf, pLineBuf);
	for (i=0; i<count; i++) {
		memset(pLineBuf, 0, LCD_PARA_LINE_LEN);
		rc = snprintf(pLineBuf, LCD_PARA_TEM_BUF_LEN, "%2x %2x %2x %2x %2x %2x  %2x ",
			priv_info->cmd_sets[type].cmds[i].msg.type, 		//data0
			priv_info->cmd_sets[type].cmds[i].last_command, 	//data1
			priv_info->cmd_sets[type].cmds[i].msg.channel,	//data2
			priv_info->cmd_sets[type].cmds[i].msg.flags&0x01,	//data3
			priv_info->cmd_sets[type].cmds[i].post_wait_ms,	//data4
			priv_info->cmd_sets[type].cmds[i].msg.tx_len&0xff00,	//data5
			priv_info->cmd_sets[type].cmds[i].msg.tx_len&0x00ff	//data6
			);
		data = (u8*)priv_info->cmd_sets[type].cmds[i].msg.tx_buf;
		for (j=0; j<priv_info->cmd_sets[type].cmds[i].msg.tx_len; j++) {
			memset(ptembuf, 0, LCD_PARA_TEM_BUF_LEN);
			rc = snprintf(ptembuf, LCD_PARA_TEM_BUF_LEN, " %2x", data[j]);
			strcat(pLineBuf, ptembuf);
		}
		strcat(pLineBuf, "\n");
		dsi_panel_param_str_append(pbuf, pLineBuf, PAGE_SIZE*2);
		//strcat(pbuf, pLineBuf);
		pr_info("%s\n", pLineBuf);
	}
	dsi_panel_param_str_append(buf, pbuf, PAGE_SIZE);
	//pr_info("cnt:%d, [%s]\n", strlen(pbuf), pbuf);
	//strcat(buf, pbuf);

}

static void dsi_display_show_panel_para(char* buf, char* pbuf, enum msm_param_id parmId, enum dsi_cmd_set_type type, struct dsi_display *display)
{
	int i,j,rc;
	u8* data;
	int count = 0;
	char ptembuf[LCD_PARA_TEM_BUF_LEN];
	char pLineBuf[LCD_PARA_LINE_LEN];

	struct panel_param *param;
	struct panel_param_val_map *param_map;
	struct dsi_panel_cmd_set *cmd_set = NULL;

	if (!display || !display->panel)
		return;

	param = &display->panel->param_cmds[parmId];
	if (!param)
		return;
	for (j = 0; j < param->val_max && param->val_max > 0 ; j++) {
		param_map = &param->val_map[j];
		if(type == param_map->type)
			cmd_set = param_map->cmds;
	}
	if (!cmd_set)
		return;

	count = cmd_set->count;

	memset(pLineBuf, 0, LCD_PARA_LINE_LEN);
	rc = snprintf(pLineBuf, LCD_PARA_LINE_LEN, "#[%s]#: count:%d\n", cmd_set_prop_map[type], count);
	pr_info("%s\n", pLineBuf);
	dsi_panel_param_str_append(pbuf, pLineBuf, PAGE_SIZE*2);
	//strcat(pbuf, pLineBuf);
	for (i=0; i<count; i++) {
		memset(pLineBuf, 0, LCD_PARA_LINE_LEN);
		rc = snprintf(pLineBuf, LCD_PARA_TEM_BUF_LEN, "%2x %2x %2x %2x %2x %2x  %2x ",
			cmd_set->cmds[i].msg.type, 		//data0
			cmd_set->cmds[i].last_command, 	//data1
			cmd_set->cmds[i].msg.channel,	//data2
			cmd_set->cmds[i].msg.flags&0x01,	//data3
			cmd_set->cmds[i].post_wait_ms,	//data4
			cmd_set->cmds[i].msg.tx_len&0xff00,	//data5
			cmd_set->cmds[i].msg.tx_len&0x00ff	//data6
			);
		data = (u8*)cmd_set->cmds[i].msg.tx_buf;
		for (j=0; j<cmd_set->cmds[i].msg.tx_len; j++) {
			memset(ptembuf, 0, LCD_PARA_TEM_BUF_LEN);
			rc = snprintf(ptembuf, LCD_PARA_TEM_BUF_LEN, " %2x", data[j]);
			strcat(pLineBuf, ptembuf);
		}
		strcat(pLineBuf, "\n");
		dsi_panel_param_str_append(pbuf, pLineBuf, PAGE_SIZE*2);
		pr_info("%s\n", pLineBuf);
	}
	//pr_info("%s\n", pbuf);
	dsi_panel_param_str_append(buf, pbuf, PAGE_SIZE);

}

static ssize_t dsi_display_parse_para_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int rc = 0;
	struct drm_connector *conn;
	struct sde_connector *sde_conn;
	struct dsi_display *display;
	u16 index = 0;
	struct dsi_mode_info *mode_timing = NULL ;
	struct dsi_display_mode *display_mode;
	struct dsi_display_mode_priv_info *priv_info;
	char* pbuf = NULL;
	char* psubbuf = NULL;
	//As size of buf is only one page(4096bytes), we can only print the param for one mode one time
	static int global_mode_index = 0;

	if (!dev || !buf) {
		pr_err("%s: Invalid input: dev(%s), buf(%s)\n", __func__, dev? "valid" : "null", buf? "valid" : "null");
		return rc;
	}

	conn = dev_get_drvdata(dev);
	sde_conn = to_sde_connector(conn);
	display = sde_conn->display;

	if (!display) {
		pr_err("Invalid input: display\n");
		return rc;
	}

	pbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (pbuf) {
		psubbuf = kzalloc(PAGE_SIZE*2, GFP_KERNEL);
		if (!psubbuf)
			return rc;

		//for (index = 0; index < display->panel->num_display_modes; index++) {

		//display_mode = &display->modes[index];
		index = global_mode_index;
		display_mode = &display->modes[global_mode_index++];
		if (global_mode_index == display->panel->num_display_modes)
			global_mode_index = 0;

		if (!display_mode) {
			pr_err("%s: Invalid input: display_mode, index %d\n", __func__, index);
			return rc;
		}
		priv_info = display_mode->priv_info;
		if (!priv_info) {
			pr_err("%s: Invalid input: priv_info, index %d\n", __func__, index);
			return rc;
		}
		mode_timing = &display_mode->timing;
		if (!mode_timing) {
			pr_err("%s: Invalid input: mode_timing, index %d\n", __func__, index);
			return rc;
		}

		memset(psubbuf, 0, PAGE_SIZE*2);
		rc = snprintf(psubbuf, PAGE_SIZE*2, "### Param for mode %d ###\n", index);
		strcat(pbuf, psubbuf);
		memset(psubbuf, 0, PAGE_SIZE*2);
		rc = snprintf(psubbuf, PAGE_SIZE*2, "h_active:%d HFP:%d HBP:%d HPW:%d h_skew:%d\n",
			mode_timing->h_active, mode_timing->h_front_porch, mode_timing->h_back_porch, mode_timing->h_sync_width, mode_timing->h_skew);
		strcat(pbuf, psubbuf);
		memset(psubbuf, 0, PAGE_SIZE*2);
		rc = snprintf(psubbuf, PAGE_SIZE*2, "V_active:%d VFP:%d VBP:%d VPW:%d\n",
			mode_timing->v_active, mode_timing->v_front_porch, mode_timing->v_back_porch, mode_timing->v_sync_width);
		strcat(pbuf, psubbuf);
		memset(psubbuf, 0, PAGE_SIZE*2);
		rc = snprintf(psubbuf, PAGE_SIZE*2, "clk rate:%d mdp_transfer_time_us:%d refresh_rate:%d\n",
			priv_info->clk_rate_hz , priv_info->mdp_transfer_time_us, mode_timing->refresh_rate);
		strcat(pbuf, psubbuf);
		pr_info("%d: %s\n", rc, pbuf);
		memset(psubbuf, 0, PAGE_SIZE*2);
		dsi_display_show_para(pbuf, psubbuf, DSI_CMD_SET_ON, priv_info);
		memset(psubbuf, 0, PAGE_SIZE*2);
		dsi_display_show_para(pbuf, psubbuf, DSI_CMD_SET_OFF, priv_info);
		memset(psubbuf, 0, PAGE_SIZE*2);
		dsi_display_show_para(pbuf, psubbuf, DSI_CMD_SET_TIMING_SWITCH, priv_info);

		memset(psubbuf, 0, PAGE_SIZE*2);
		dsi_display_show_panel_para(pbuf, psubbuf, PARAM_DC_ID, DSI_CMD_SET_DC_ON, display);
		memset(psubbuf, 0, PAGE_SIZE*2);
		dsi_display_show_panel_para(pbuf, psubbuf, PARAM_DC_ID, DSI_CMD_SET_DC_OFF, display);

		//}
		//dsi_panel_param_str_append(buf, pbuf, PAGE_SIZE);
		//pr_info("count:%d: [%s]\n", strlen(pbuf), pbuf);
		if (strlen(pbuf) < PAGE_SIZE)
		    rc = scnprintf(buf, PAGE_SIZE, "%s\n", pbuf);
		else
		    strncpy(buf, pbuf, PAGE_SIZE-1);
		//pr_info("%d: %s\n", rc, buf);
	} else {
		pr_warn("kmalloc failed\n" );
	}

	if (psubbuf)
		kfree(psubbuf);
	if (pbuf)
		kfree(pbuf);
	return rc;
}

// val 0: update the paras for the first modes
static ssize_t dsi_display_parse_para_update(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *conn;
	struct sde_connector *sde_conn;
	struct dsi_display *display;
	u16 index = 0;

	if (!dev || !buf) {
		pr_err("%s: Invalid input: dev(%s), buf(%s)\n", __func__, dev? "valid" : "null", buf? "valid" : "null");
		return count;
	}

	conn = dev_get_drvdata(dev);
	sde_conn = to_sde_connector(conn);
	display = sde_conn->display;
	if (!display) {
		pr_err("Invalid  input: display\n");
		return count;
	}

	if (kstrtou16(buf, 10, &index) < 0)
		return count;

	dsi_panel_mot_parse_timing_from_file(display, index);

	return count;
}

static ssize_t dsi_display_para_by_id_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int rc = 0;
	struct drm_connector *conn;
	struct sde_connector *sde_conn;
	struct dsi_display *display;
	char* pbuf = NULL;
	char* psubbuf = NULL;

	if (!dev || !buf) {
		pr_err("%s: Invalid input: dev(%s), buf(%s)\n", __func__, dev? "valid" : "null", buf? "valid" : "null");
		return rc;
	}

	conn = dev_get_drvdata(dev);
	sde_conn = to_sde_connector(conn);
	display = sde_conn->display;

	if (!display) {
		pr_err("Invalid  input: display\n");
		return rc;
	}

	pbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (pbuf) {
		psubbuf = kzalloc(PAGE_SIZE*2, GFP_KERNEL);
		if (!psubbuf)
			return rc;

		memset(psubbuf, 0, PAGE_SIZE*2);
		dsi_display_show_panel_para(pbuf, psubbuf, g_param_id_flag/100, g_param_id_flag%100, display);

		//rc = scnprintf(buf, PAGE_SIZE, "%s\n", pbuf);
		if (strlen(pbuf) < PAGE_SIZE)
		    rc = scnprintf(buf, PAGE_SIZE, "%s\n", pbuf);
		else
		    strncpy(buf, pbuf, PAGE_SIZE-1);
		//pr_info("%d: %s\n", rc, buf);
	} else {
		pr_warn("kmalloc failed\n" );
	}

	if (psubbuf)
		kfree(psubbuf);
	if (pbuf)
		kfree(pbuf);
	return rc;
}

static ssize_t dsi_display_para_by_id_put(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *conn;
	struct sde_connector *sde_conn;
	struct dsi_display *display;

	if (!dev || !buf) {
		pr_err("%s: Invalid input: dev(%s), buf(%s)\n", __func__, dev? "valid" : "null", buf? "valid" : "null");
		return count;
	}

	conn = dev_get_drvdata(dev);
	sde_conn = to_sde_connector(conn);
	display = sde_conn->display;
	if (!display) {
		pr_err("Invalid  input: display\n");
		return count;
	}

	if (kstrtou32(buf, 10, &g_param_id_flag) < 0)
		return count;

	return count;
}


///sys/devices/platform/soc/soc:qcom,dsi-display/
static DEVICE_ATTR(dsi_display_wakeup, 0644,
			dsi_display_wakup_get,
			dsi_display_wakup_set);
static DEVICE_ATTR(pressure_test_en, 0644,
			dsi_display_pressure_test_en_get,
			dsi_display_pressure_test_en_set);
static DEVICE_ATTR(panel_parse_para, 0664,
			dsi_display_parse_para_get,
			dsi_display_parse_para_update);
static DEVICE_ATTR(panel_para_by_id, 0664,
			dsi_display_para_by_id_get,
			dsi_display_para_by_id_put);


static const struct attribute *dsi_display_mot_ext_fs_attrs[] = {
	&dev_attr_dsi_display_wakeup.attr,
	&dev_attr_pressure_test_en.attr,
	&dev_attr_panel_parse_para.attr,
	&dev_attr_panel_para_by_id.attr,
	NULL,
};

static int dsi_display_sysfs_ext_init(struct dsi_display *display)
{
	int rc = 0;
	if (!display || !display->drm_conn || !display->drm_conn->kdev) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	rc = sysfs_create_files(&display->drm_conn->kdev->kobj, dsi_display_mot_ext_fs_attrs);

	return rc;
}

int dsi_display_ext_init(struct dsi_display *display)
{
	int rc = 0;


	dsi_display_sysfs_ext_init(display);

	pr_info("dsi_display_ext_init success\n");

	return rc;
}

