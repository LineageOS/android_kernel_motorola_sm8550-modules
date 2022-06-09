// SPDX-License-Identifier: GPL-2.0-only
/*
 * QTI CE device driver.
 *
 * Copyright (c) 2010-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/mman.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include "linux/platform_data/qcom_crypto_device.h"
#include "linux/qcedev.h"
#include <linux/interconnect.h>

#include <crypto/hash.h>
#include "qcedevi.h"
#include "qce.h"
#include "qcedev_smmu.h"
#include "compat_qcedev.h"

#include <linux/compat.h>

#define CACHE_LINE_SIZE 64
#define CE_SHA_BLOCK_SIZE SHA256_BLOCK_SIZE
#define MAX_CEHW_REQ_TRANSFER_SIZE (128*32*1024)
/* Max wait time once a crypt o request is done */
#define MAX_CRYPTO_WAIT_TIME 1500

static uint8_t  _std_init_vector_sha1_uint8[] =   {
	0x67, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB, 0x89,
	0x98, 0xBA, 0xDC, 0xFE, 0x10, 0x32, 0x54, 0x76,
	0xC3, 0xD2, 0xE1, 0xF0
};
/* standard initialization vector for SHA-256, source: FIPS 180-2 */
static uint8_t _std_init_vector_sha256_uint8[] = {
	0x6A, 0x09, 0xE6, 0x67, 0xBB, 0x67, 0xAE, 0x85,
	0x3C, 0x6E, 0xF3, 0x72, 0xA5, 0x4F, 0xF5, 0x3A,
	0x51, 0x0E, 0x52, 0x7F, 0x9B, 0x05, 0x68, 0x8C,
	0x1F, 0x83, 0xD9, 0xAB, 0x5B, 0xE0, 0xCD, 0x19
};

#define QCEDEV_CTX_KEY_MASK		0x000000ff
#define QCEDEV_CTX_USE_HW_KEY		0x00000001
#define QCEDEV_CTX_USE_PIPE_KEY		0x00000002

// Key timer expiry for pipes 1-15 (Status3)
#define PIPE_KEY_TIMER_EXPIRED_STATUS3_MASK	0x000000FF
// Key timer expiry for pipes 16-19 (Status6)
#define PIPE_KEY_TIMER_EXPIRED_STATUS6_MASK	0x00000003
// Key pause for pipes 1-15 (Status3)
#define PIPE_KEY_PAUSE_STATUS3_MASK	0xFF0000
// Key pause for pipes 16-19 (Status6)
#define PIPE_KEY_PAUSE_STATUS6_MASK	0x30000

#define QCEDEV_STATUS1_ERR_INTR_MASK	0x10

static DEFINE_MUTEX(send_cmd_lock);
static DEFINE_MUTEX(qcedev_sent_bw_req);
static DEFINE_MUTEX(hash_access_lock);

static dev_t qcedev_device_no;
static struct class *driver_class;
static struct device *class_dev;

static const struct of_device_id qcedev_match[] = {
	{	.compatible = "qcom,qcedev"},
	{	.compatible = "qcom,qcedev,context-bank"},
	{}
};

MODULE_DEVICE_TABLE(of, qcedev_match);

static int qcedev_control_clocks(struct qcedev_control *podev, bool enable)
{
	unsigned int control_flag;
	int ret = 0;

	if (podev->ce_support.req_bw_before_clk) {
		if (enable)
			control_flag = QCE_BW_REQUEST_FIRST;
		else
			control_flag = QCE_CLK_DISABLE_FIRST;
	} else {
		if (enable)
			control_flag = QCE_CLK_ENABLE_FIRST;
		else
			control_flag = QCE_BW_REQUEST_RESET_FIRST;
	}

	switch (control_flag) {
	case QCE_CLK_ENABLE_FIRST:
		ret = qce_enable_clk(podev->qce);
		if (ret) {
			pr_err("%s Unable enable clk\n", __func__);
			return ret;
		}
		ret = icc_set_bw(podev->icc_path,
				CRYPTO_AVG_BW, CRYPTO_PEAK_BW);
		if (ret) {
			pr_err("%s Unable to set high bw\n", __func__);
			ret = qce_disable_clk(podev->qce);
			if (ret)
				pr_err("%s Unable disable clk\n", __func__);
			return ret;
		}
		break;
	case QCE_BW_REQUEST_FIRST:
		ret = icc_set_bw(podev->icc_path,
				CRYPTO_AVG_BW, CRYPTO_PEAK_BW);
		if (ret) {
			pr_err("%s Unable to set high bw\n", __func__);
			return ret;
		}
		ret = qce_enable_clk(podev->qce);
		if (ret) {
			pr_err("%s Unable enable clk\n", __func__);
			ret = icc_set_bw(podev->icc_path, 0, 0);
			if (ret)
				pr_err("%s Unable to set low bw\n", __func__);
			return ret;
		}
		break;
	case QCE_CLK_DISABLE_FIRST:
		ret = qce_disable_clk(podev->qce);
		if (ret) {
			pr_err("%s Unable to disable clk\n", __func__);
			return ret;
		}
		ret = icc_set_bw(podev->icc_path, 0, 0);
		if (ret) {
			pr_err("%s Unable to set low bw\n", __func__);
			ret = qce_enable_clk(podev->qce);
			if (ret)
				pr_err("%s Unable enable clk\n", __func__);
			return ret;
		}
		break;
	case QCE_BW_REQUEST_RESET_FIRST:
		ret = icc_set_bw(podev->icc_path, 0, 0);
		if (ret) {
			pr_err("%s Unable to set low bw\n", __func__);
			return ret;
		}
		ret = qce_disable_clk(podev->qce);
		if (ret) {
			pr_err("%s Unable to disable clk\n", __func__);
			ret = icc_set_bw(podev->icc_path,
					CRYPTO_AVG_BW, CRYPTO_PEAK_BW);
			if (ret)
				pr_err("%s Unable to set high bw\n", __func__);
			return ret;
		}
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static void qcedev_ce_high_bw_req(struct qcedev_control *podev,
							bool high_bw_req)
{
	int ret = 0;

	mutex_lock(&qcedev_sent_bw_req);
	if (high_bw_req) {
		if (podev->high_bw_req_count == 0) {
			ret = qcedev_control_clocks(podev, true);
			if (ret)
				goto exit_unlock_mutex;
		}
		podev->high_bw_req_count++;
	} else {
		if (podev->high_bw_req_count == 1) {
			ret = qcedev_control_clocks(podev, false);
			if (ret)
				goto exit_unlock_mutex;
		}
		podev->high_bw_req_count--;
	}

exit_unlock_mutex:
	mutex_unlock(&qcedev_sent_bw_req);
}

#define QCEDEV_MAGIC 0x56434544 /* "qced" */

static int qcedev_open(struct inode *inode, struct file *file);
static int qcedev_release(struct inode *inode, struct file *file);
static int start_cipher_req(struct qcedev_control *podev,
			    int *current_req_info);
static int start_offload_cipher_req(struct qcedev_control *podev,
				int *current_req_info);
static int start_sha_req(struct qcedev_control *podev,
			 int *current_req_info);

static const struct file_operations qcedev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = qcedev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_qcedev_ioctl,
#endif
	.open = qcedev_open,
	.release = qcedev_release,
};

static struct qcedev_control qce_dev[] = {
	{
		.magic = QCEDEV_MAGIC,
	},
};

#define MAX_QCE_DEVICE ARRAY_SIZE(qce_dev)
#define DEBUG_MAX_FNAME  16
#define DEBUG_MAX_RW_BUF 1024

struct qcedev_stat {
	u32 qcedev_dec_success;
	u32 qcedev_dec_fail;
	u32 qcedev_enc_success;
	u32 qcedev_enc_fail;
	u32 qcedev_sha_success;
	u32 qcedev_sha_fail;
};

static struct qcedev_stat _qcedev_stat;
static struct dentry *_debug_dent;
static char _debug_read_buf[DEBUG_MAX_RW_BUF];
static int _debug_qcedev;

static struct qcedev_control *qcedev_minor_to_control(unsigned int n)
{
	int i;

	for (i = 0; i < MAX_QCE_DEVICE; i++) {
		if (qce_dev[i].minor == n)
			return &qce_dev[n];
	}
	return NULL;
}

static int qcedev_open(struct inode *inode, struct file *file)
{
	struct qcedev_handle *handle;
	struct qcedev_control *podev;

	podev = qcedev_minor_to_control(MINOR(inode->i_rdev));
	if (podev == NULL) {
		pr_err("%s: no such device %d\n", __func__,
					MINOR(inode->i_rdev));
		return -ENOENT;
	}

	handle = kzalloc(sizeof(struct qcedev_handle), GFP_KERNEL);
	if (handle == NULL)
		return -ENOMEM;

	handle->cntl = podev;
	file->private_data = handle;

	qcedev_ce_high_bw_req(podev, true);

	mutex_init(&handle->registeredbufs.lock);
	INIT_LIST_HEAD(&handle->registeredbufs.list);
	return 0;
}

static int qcedev_release(struct inode *inode, struct file *file)
{
	struct qcedev_control *podev;
	struct qcedev_handle *handle;

	handle =  file->private_data;
	podev =  handle->cntl;
	if (podev != NULL && podev->magic != QCEDEV_MAGIC) {
		pr_err("%s: invalid handle %pK\n",
					__func__, podev);
	}

	qcedev_ce_high_bw_req(podev, false);
	if (qcedev_unmap_all_buffers(handle))
		pr_err("%s: failed to unmap all ion buffers\n", __func__);

	kfree_sensitive(handle);
	file->private_data = NULL;
	return 0;
}

static void req_done(unsigned long data)
{
	struct qcedev_control *podev = (struct qcedev_control *)data;
	struct qcedev_async_req *areq;
	unsigned long flags = 0;
	struct qcedev_async_req *new_req = NULL;
	int ret = 0;
	int current_req_info = 0;

	spin_lock_irqsave(&podev->lock, flags);
	areq = podev->active_command;
	podev->active_command = NULL;

again:
	if (!list_empty(&podev->ready_commands)) {
		new_req = container_of(podev->ready_commands.next,
						struct qcedev_async_req, list);
		list_del(&new_req->list);
		podev->active_command = new_req;
		new_req->err = 0;
		if (new_req->op_type == QCEDEV_CRYPTO_OPER_CIPHER)
			ret = start_cipher_req(podev, &current_req_info);
		else if (new_req->op_type == QCEDEV_CRYPTO_OPER_OFFLOAD_CIPHER)
			ret = start_offload_cipher_req(podev, &current_req_info);
		else
			ret = start_sha_req(podev, &current_req_info);
	}

	spin_unlock_irqrestore(&podev->lock, flags);

	if (areq)
		complete(&areq->complete);

	if (new_req && ret) {
		complete(&new_req->complete);
		spin_lock_irqsave(&podev->lock, flags);
		podev->active_command = NULL;
		areq = NULL;
		ret = 0;
		new_req = NULL;
		goto again;
	}
}

void qcedev_sha_req_cb(void *cookie, unsigned char *digest,
	unsigned char *authdata, int ret)
{
	struct qcedev_sha_req *areq;
	struct qcedev_control *pdev;
	struct qcedev_handle *handle;

	uint32_t *auth32 = (uint32_t *)authdata;

	areq = (struct qcedev_sha_req *) cookie;
	handle = (struct qcedev_handle *) areq->cookie;
	pdev = handle->cntl;

	if (digest)
		memcpy(&handle->sha_ctxt.digest[0], digest, 32);

	if (authdata) {
		handle->sha_ctxt.auth_data[0] = auth32[0];
		handle->sha_ctxt.auth_data[1] = auth32[1];
	}

	tasklet_schedule(&pdev->done_tasklet);
};


void qcedev_cipher_req_cb(void *cookie, unsigned char *icv,
	unsigned char *iv, int ret)
{
	struct qcedev_cipher_req *areq;
	struct qcedev_handle *handle;
	struct qcedev_control *podev;
	struct qcedev_async_req *qcedev_areq;

	areq = (struct qcedev_cipher_req *) cookie;
	handle = (struct qcedev_handle *) areq->cookie;
	podev = handle->cntl;
	qcedev_areq = podev->active_command;

	if (iv)
		memcpy(&qcedev_areq->cipher_op_req.iv[0], iv,
					qcedev_areq->cipher_op_req.ivlen);
	tasklet_schedule(&podev->done_tasklet);
};

static int start_cipher_req(struct qcedev_control *podev,
			    int *current_req_info)
{
	struct qcedev_async_req *qcedev_areq;
	struct qce_req creq;
	int ret = 0;

	/* start the command on the podev->active_command */
	qcedev_areq = podev->active_command;
	qcedev_areq->cipher_req.cookie = qcedev_areq->handle;
	if (qcedev_areq->cipher_op_req.use_pmem == QCEDEV_USE_PMEM) {
		pr_err("%s: Use of PMEM is not supported\n", __func__);
		goto unsupported;
	}
	creq.pmem = NULL;
	switch (qcedev_areq->cipher_op_req.alg) {
	case QCEDEV_ALG_DES:
		creq.alg = CIPHER_ALG_DES;
		break;
	case QCEDEV_ALG_3DES:
		creq.alg = CIPHER_ALG_3DES;
		break;
	case QCEDEV_ALG_AES:
		creq.alg = CIPHER_ALG_AES;
		break;
	default:
		return -EINVAL;
	}

	switch (qcedev_areq->cipher_op_req.mode) {
	case QCEDEV_AES_MODE_CBC:
	case QCEDEV_DES_MODE_CBC:
		creq.mode = QCE_MODE_CBC;
		break;
	case QCEDEV_AES_MODE_ECB:
	case QCEDEV_DES_MODE_ECB:
		creq.mode = QCE_MODE_ECB;
		break;
	case QCEDEV_AES_MODE_CTR:
		creq.mode = QCE_MODE_CTR;
		break;
	case QCEDEV_AES_MODE_XTS:
		creq.mode = QCE_MODE_XTS;
		break;
	default:
		return -EINVAL;
	}

	if ((creq.alg == CIPHER_ALG_AES) &&
		(creq.mode == QCE_MODE_CTR)) {
		creq.dir = QCE_ENCRYPT;
	} else {
		if (qcedev_areq->cipher_op_req.op == QCEDEV_OPER_ENC)
			creq.dir = QCE_ENCRYPT;
		else
			creq.dir = QCE_DECRYPT;
	}

	creq.iv = &qcedev_areq->cipher_op_req.iv[0];
	creq.ivsize = qcedev_areq->cipher_op_req.ivlen;

	creq.enckey =  &qcedev_areq->cipher_op_req.enckey[0];
	creq.encklen = qcedev_areq->cipher_op_req.encklen;

	creq.cryptlen = qcedev_areq->cipher_op_req.data_len;

	if (qcedev_areq->cipher_op_req.encklen == 0) {
		if ((qcedev_areq->cipher_op_req.op == QCEDEV_OPER_ENC_NO_KEY)
			|| (qcedev_areq->cipher_op_req.op ==
				QCEDEV_OPER_DEC_NO_KEY))
			creq.op = QCE_REQ_ABLK_CIPHER_NO_KEY;
		else {
			int i;

			for (i = 0; i < QCEDEV_MAX_KEY_SIZE; i++) {
				if (qcedev_areq->cipher_op_req.enckey[i] != 0)
					break;
			}

			if ((podev->platform_support.hw_key_support == 1) &&
						(i == QCEDEV_MAX_KEY_SIZE))
				creq.op = QCE_REQ_ABLK_CIPHER;
			else {
				ret = -EINVAL;
				goto unsupported;
			}
		}
	} else {
		creq.op = QCE_REQ_ABLK_CIPHER;
	}

	creq.qce_cb = qcedev_cipher_req_cb;
	creq.areq = (void *)&qcedev_areq->cipher_req;
	creq.flags = 0;
	creq.offload_op = 0;
	ret = qce_ablk_cipher_req(podev->qce, &creq);
	*current_req_info = creq.current_req_info;
unsupported:
	qcedev_areq->err = ret ? -ENXIO : 0;

	return ret;
};

void qcedev_offload_cipher_req_cb(void *cookie, unsigned char *icv,
			      unsigned char *iv, int ret)
{
	struct qcedev_cipher_req *areq;
	struct qcedev_handle *handle;
	struct qcedev_control *podev;
	struct qcedev_async_req *qcedev_areq;

	areq = (struct qcedev_cipher_req *) cookie;
	handle = (struct qcedev_handle *) areq->cookie;
	podev = handle->cntl;
	qcedev_areq = podev->active_command;

	if (iv)
		memcpy(&qcedev_areq->offload_cipher_op_req.iv[0], iv,
			qcedev_areq->offload_cipher_op_req.ivlen);

	tasklet_schedule(&podev->done_tasklet);
}

static int start_offload_cipher_req(struct qcedev_control *podev,
				int *current_req_info)
{
	struct qcedev_async_req *qcedev_areq;
	struct qce_req creq;
	u8 patt_sz = 0, proc_data_sz = 0;
	int ret = 0;

	memset(&creq, 0, sizeof(creq));
	/* Start the command on the podev->active_command */
	qcedev_areq = podev->active_command;
	qcedev_areq->cipher_req.cookie = qcedev_areq->handle;

	switch (qcedev_areq->offload_cipher_op_req.alg) {
	case QCEDEV_ALG_AES:
		creq.alg = CIPHER_ALG_AES;
		break;
	default:
		return -EINVAL;
	}

	switch (qcedev_areq->offload_cipher_op_req.mode) {
	case QCEDEV_AES_MODE_CBC:
		creq.mode = QCE_MODE_CBC;
		break;
	case QCEDEV_AES_MODE_CTR:
		creq.mode = QCE_MODE_CTR;
		break;
	default:
		return -EINVAL;
	}

	if (qcedev_areq->offload_cipher_op_req.is_copy_op) {
		creq.dir = QCE_ENCRYPT;
	} else {
		switch(qcedev_areq->offload_cipher_op_req.op) {
		case QCEDEV_OFFLOAD_HLOS_HLOS:
		case QCEDEV_OFFLOAD_HLOS_CPB:
			creq.dir = QCE_DECRYPT;
			break;
		case QCEDEV_OFFLOAD_CPB_HLOS:
			creq.dir = QCE_ENCRYPT;
			break;
		default:
			return -EINVAL;
		}
	}

	creq.iv = &qcedev_areq->offload_cipher_op_req.iv[0];
	creq.ivsize = qcedev_areq->offload_cipher_op_req.ivlen;
	creq.iv_ctr_size = qcedev_areq->offload_cipher_op_req.iv_ctr_size;

	creq.encklen = qcedev_areq->offload_cipher_op_req.encklen;

	/* OFFLOAD use cases use PIPE keys so no need to set keys */
	creq.flags = QCEDEV_CTX_USE_PIPE_KEY;
	creq.op = QCE_REQ_ABLK_CIPHER_NO_KEY;
	creq.offload_op = (int)qcedev_areq->offload_cipher_op_req.op;
	if (qcedev_areq->offload_cipher_op_req.is_copy_op)
		creq.is_copy_op = true;

	creq.cryptlen = qcedev_areq->offload_cipher_op_req.data_len;

	creq.qce_cb = qcedev_offload_cipher_req_cb;
	creq.areq = (void *)&qcedev_areq->cipher_req;

	patt_sz = qcedev_areq->offload_cipher_op_req.pattern_info.patt_sz;
	proc_data_sz =
		qcedev_areq->offload_cipher_op_req.pattern_info.proc_data_sz;
	creq.is_pattern_valid =
		qcedev_areq->offload_cipher_op_req.is_pattern_valid;
	if (creq.is_pattern_valid) {
		creq.pattern_info = 0x1;
		if (patt_sz)
			creq.pattern_info |= (patt_sz - 1) << 4;
		if (proc_data_sz)
			creq.pattern_info |= (proc_data_sz - 1) << 8;
		creq.pattern_info |=
		qcedev_areq->offload_cipher_op_req.pattern_info.patt_offset << 12;
	}
	creq.block_offset = qcedev_areq->offload_cipher_op_req.block_offset;
	ret = qce_ablk_cipher_req(podev->qce, &creq);

	*current_req_info = creq.current_req_info;
	qcedev_areq->err = ret ? -ENXIO : 0;

	return ret;
}

static int start_sha_req(struct qcedev_control *podev,
			 int *current_req_info)
{
	struct qcedev_async_req *qcedev_areq;
	struct qce_sha_req sreq;
	int ret = 0;
	struct qcedev_handle *handle;

	/* start the command on the podev->active_command */
	qcedev_areq = podev->active_command;
	handle = qcedev_areq->handle;

	switch (qcedev_areq->sha_op_req.alg) {
	case QCEDEV_ALG_SHA1:
		sreq.alg = QCE_HASH_SHA1;
		break;
	case QCEDEV_ALG_SHA256:
		sreq.alg = QCE_HASH_SHA256;
		break;
	case QCEDEV_ALG_SHA1_HMAC:
		if (podev->ce_support.sha_hmac) {
			sreq.alg = QCE_HASH_SHA1_HMAC;
			sreq.authkey = &handle->sha_ctxt.authkey[0];
			sreq.authklen = QCEDEV_MAX_SHA_BLOCK_SIZE;

		} else {
			sreq.alg = QCE_HASH_SHA1;
			sreq.authkey = NULL;
		}
		break;
	case QCEDEV_ALG_SHA256_HMAC:
		if (podev->ce_support.sha_hmac) {
			sreq.alg = QCE_HASH_SHA256_HMAC;
			sreq.authkey = &handle->sha_ctxt.authkey[0];
			sreq.authklen = QCEDEV_MAX_SHA_BLOCK_SIZE;
		} else {
			sreq.alg = QCE_HASH_SHA256;
			sreq.authkey = NULL;
		}
		break;
	case QCEDEV_ALG_AES_CMAC:
		sreq.alg = QCE_HASH_AES_CMAC;
		sreq.authkey = &handle->sha_ctxt.authkey[0];
		sreq.authklen = qcedev_areq->sha_op_req.authklen;
		break;
	default:
		pr_err("Algorithm %d not supported, exiting\n",
			qcedev_areq->sha_op_req.alg);
		return -EINVAL;
	}

	qcedev_areq->sha_req.cookie = handle;

	sreq.qce_cb = qcedev_sha_req_cb;
	if (qcedev_areq->sha_op_req.alg != QCEDEV_ALG_AES_CMAC) {
		sreq.auth_data[0] = handle->sha_ctxt.auth_data[0];
		sreq.auth_data[1] = handle->sha_ctxt.auth_data[1];
		sreq.auth_data[2] = handle->sha_ctxt.auth_data[2];
		sreq.auth_data[3] = handle->sha_ctxt.auth_data[3];
		sreq.digest = &handle->sha_ctxt.digest[0];
		sreq.first_blk = handle->sha_ctxt.first_blk;
		sreq.last_blk = handle->sha_ctxt.last_blk;
	}
	sreq.size = qcedev_areq->sha_req.sreq.nbytes;
	sreq.src = qcedev_areq->sha_req.sreq.src;
	sreq.areq = (void *)&qcedev_areq->sha_req;
	sreq.flags = 0;

	ret = qce_process_sha_req(podev->qce, &sreq);

	*current_req_info = sreq.current_req_info;
	qcedev_areq->err = ret ? -ENXIO : 0;

	return ret;
};

static void qcedev_check_crypto_status(
			struct qcedev_async_req *qcedev_areq, void *handle,
			bool print_err)
{
	unsigned int s1, s2, s3, s4, s5, s6;

	qcedev_areq->offload_cipher_op_req.err = QCEDEV_OFFLOAD_NO_ERROR;
	qce_get_crypto_status(handle, &s1, &s2, &s3, &s4, &s5, &s6);

	if (print_err) {
		pr_err("%s: sts = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", __func__,
			s1, s2, s3, s4, s5, s6);
	}

	// Check for key timer expiry
	if ((s6 & PIPE_KEY_TIMER_EXPIRED_STATUS6_MASK) ||
		(s3 & PIPE_KEY_TIMER_EXPIRED_STATUS3_MASK)) {
		pr_info("%s: crypto timer expired\n", __func__);
		pr_info("%s: sts = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", __func__,
			s1, s2, s3, s4, s5, s6);
		qcedev_areq->offload_cipher_op_req.err =
					QCEDEV_OFFLOAD_KEY_TIMER_EXPIRED_ERROR;
		return;
	}

	// Check for key pause
	if ((s6 & PIPE_KEY_PAUSE_STATUS6_MASK) ||
		(s3 & PIPE_KEY_PAUSE_STATUS3_MASK)) {
		pr_info("%s: crypto key paused\n", __func__);
		pr_info("%s: sts = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", __func__,
			s1, s2, s3, s4, s5, s6);
		qcedev_areq->offload_cipher_op_req.err =
					QCEDEV_OFFLOAD_KEY_PAUSE_ERROR;
		return;
	}

	// Check for generic error
	if (s1 & QCEDEV_STATUS1_ERR_INTR_MASK) {
		pr_err("%s: generic crypto error\n", __func__);
		pr_info("%s: sts = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", __func__,
			s1, s2, s3, s4, s5, s6);
		qcedev_areq->offload_cipher_op_req.err =
					QCEDEV_OFFLOAD_GENERIC_ERROR;
		return;
	}
}

static int submit_req(struct qcedev_async_req *qcedev_areq,
					struct qcedev_handle *handle)
{
	struct qcedev_control *podev;
	unsigned long flags = 0;
	int ret = 0;
	struct qcedev_stat *pstat;
	int current_req_info = 0;
	int wait = 0;
	bool print_sts = false;

	qcedev_areq->err = 0;
	podev = handle->cntl;

	spin_lock_irqsave(&podev->lock, flags);

	if (podev->active_command == NULL) {
		podev->active_command = qcedev_areq;
		if (qcedev_areq->op_type == QCEDEV_CRYPTO_OPER_CIPHER)
			ret = start_cipher_req(podev, &current_req_info);
		else if (qcedev_areq->op_type == QCEDEV_CRYPTO_OPER_OFFLOAD_CIPHER)
			ret = start_offload_cipher_req(podev, &current_req_info);
		else
			ret = start_sha_req(podev, &current_req_info);
	} else {
		list_add_tail(&qcedev_areq->list, &podev->ready_commands);
	}

	if (ret != 0)
		podev->active_command = NULL;

	spin_unlock_irqrestore(&podev->lock, flags);

	if (ret == 0)
		wait = wait_for_completion_timeout(&qcedev_areq->complete,
				msecs_to_jiffies(MAX_CRYPTO_WAIT_TIME));

	if (!wait) {
	/*
	 * This means wait timed out, and the callback routine was not
	 * exercised. The callback sequence does some housekeeping which
	 * would be missed here, hence having a call to qce here to do
	 * that.
	 */
		pr_err("%s: wait timed out, req info = %d\n", __func__,
					current_req_info);
		print_sts = true;
		qcedev_check_crypto_status(qcedev_areq, podev->qce, print_sts);
		qce_manage_timeout(podev->qce, current_req_info);
		if (qcedev_areq->offload_cipher_op_req.err !=
						QCEDEV_OFFLOAD_NO_ERROR)
			return 0;
	}

	if (ret)
		qcedev_areq->err = -EIO;

	pstat = &_qcedev_stat;
	if (qcedev_areq->op_type == QCEDEV_CRYPTO_OPER_CIPHER) {
		switch (qcedev_areq->cipher_op_req.op) {
		case QCEDEV_OPER_DEC:
			if (qcedev_areq->err)
				pstat->qcedev_dec_fail++;
			else
				pstat->qcedev_dec_success++;
			break;
		case QCEDEV_OPER_ENC:
			if (qcedev_areq->err)
				pstat->qcedev_enc_fail++;
			else
				pstat->qcedev_enc_success++;
			break;
		default:
			break;
		}
	} else if (qcedev_areq->op_type == QCEDEV_CRYPTO_OPER_OFFLOAD_CIPHER) {
		//Do nothing
	} else {
		if (qcedev_areq->err)
			pstat->qcedev_sha_fail++;
		else
			pstat->qcedev_sha_success++;
	}

	return qcedev_areq->err;
}

static int qcedev_sha_init(struct qcedev_async_req *areq,
				struct qcedev_handle *handle)
{
	struct qcedev_sha_ctxt *sha_ctxt = &handle->sha_ctxt;

	memset(sha_ctxt, 0, sizeof(struct qcedev_sha_ctxt));
	sha_ctxt->first_blk = 1;

	if ((areq->sha_op_req.alg == QCEDEV_ALG_SHA1) ||
			(areq->sha_op_req.alg == QCEDEV_ALG_SHA1_HMAC)) {
		memcpy(&sha_ctxt->digest[0],
			&_std_init_vector_sha1_uint8[0], SHA1_DIGEST_SIZE);
		sha_ctxt->diglen = SHA1_DIGEST_SIZE;
	} else {
		if ((areq->sha_op_req.alg == QCEDEV_ALG_SHA256) ||
			(areq->sha_op_req.alg == QCEDEV_ALG_SHA256_HMAC)) {
			memcpy(&sha_ctxt->digest[0],
					&_std_init_vector_sha256_uint8[0],
					SHA256_DIGEST_SIZE);
			sha_ctxt->diglen = SHA256_DIGEST_SIZE;
		}
	}
	sha_ctxt->init_done = true;
	return 0;
}


static int qcedev_sha_update_max_xfer(struct qcedev_async_req *qcedev_areq,
				struct qcedev_handle *handle,
				struct scatterlist *sg_src)
{
	int err = 0;
	int i = 0;
	uint32_t total;

	uint8_t *user_src = NULL;
	uint8_t *k_src = NULL;
	uint8_t *k_buf_src = NULL;
	uint8_t *k_align_src = NULL;

	uint32_t sha_pad_len = 0;
	uint32_t trailing_buf_len = 0;
	uint32_t t_buf = handle->sha_ctxt.trailing_buf_len;
	uint32_t sha_block_size;

	total = qcedev_areq->sha_op_req.data_len + t_buf;

	if (qcedev_areq->sha_op_req.alg == QCEDEV_ALG_SHA1)
		sha_block_size = SHA1_BLOCK_SIZE;
	else
		sha_block_size = SHA256_BLOCK_SIZE;

	if (total <= sha_block_size) {
		uint32_t len =  qcedev_areq->sha_op_req.data_len;

		i = 0;

		k_src = &handle->sha_ctxt.trailing_buf[t_buf];

		/* Copy data from user src(s) */
		while (len > 0) {
			user_src = qcedev_areq->sha_op_req.data[i].vaddr;
			if (user_src && copy_from_user(k_src,
				(void __user *)user_src,
				qcedev_areq->sha_op_req.data[i].len))
				return -EFAULT;

			len -= qcedev_areq->sha_op_req.data[i].len;
			k_src += qcedev_areq->sha_op_req.data[i].len;
			i++;
		}
		handle->sha_ctxt.trailing_buf_len = total;

		return 0;
	}


	k_buf_src = kmalloc(total + CACHE_LINE_SIZE * 2,
				GFP_KERNEL);
	if (k_buf_src == NULL)
		return -ENOMEM;

	k_align_src = (uint8_t *)ALIGN(((uintptr_t)k_buf_src),
							CACHE_LINE_SIZE);
	k_src = k_align_src;

	/* check for trailing buffer from previous updates and append it */
	if (t_buf > 0) {
		memcpy(k_src, &handle->sha_ctxt.trailing_buf[0],
								t_buf);
		k_src += t_buf;
	}

	/* Copy data from user src(s) */
	user_src = qcedev_areq->sha_op_req.data[0].vaddr;
	if (user_src && copy_from_user(k_src,
				(void __user *)user_src,
				qcedev_areq->sha_op_req.data[0].len)) {
		memset(k_buf_src, 0, ksize((void *)k_buf_src));
		kfree(k_buf_src);
		return -EFAULT;
	}
	k_src += qcedev_areq->sha_op_req.data[0].len;
	for (i = 1; i < qcedev_areq->sha_op_req.entries; i++) {
		user_src = qcedev_areq->sha_op_req.data[i].vaddr;
		if (user_src && copy_from_user(k_src,
					(void __user *)user_src,
					qcedev_areq->sha_op_req.data[i].len)) {
			memset(k_buf_src, 0, ksize((void *)k_buf_src));
			kfree(k_buf_src);
			return -EFAULT;
		}
		k_src += qcedev_areq->sha_op_req.data[i].len;
	}

	/*  get new trailing buffer */
	sha_pad_len = ALIGN(total, CE_SHA_BLOCK_SIZE) - total;
	trailing_buf_len =  CE_SHA_BLOCK_SIZE - sha_pad_len;

	qcedev_areq->sha_req.sreq.src = sg_src;
	sg_init_one(qcedev_areq->sha_req.sreq.src, k_align_src,
						total-trailing_buf_len);

	qcedev_areq->sha_req.sreq.nbytes = total - trailing_buf_len;

	/*  update sha_ctxt trailing buf content to new trailing buf */
	if (trailing_buf_len > 0) {
		memset(&handle->sha_ctxt.trailing_buf[0], 0, 64);
		memcpy(&handle->sha_ctxt.trailing_buf[0],
			(k_src - trailing_buf_len),
			trailing_buf_len);
	}
	handle->sha_ctxt.trailing_buf_len = trailing_buf_len;

	err = submit_req(qcedev_areq, handle);

	handle->sha_ctxt.last_blk = 0;
	handle->sha_ctxt.first_blk = 0;

	memset(k_buf_src, 0, ksize((void *)k_buf_src));
	kfree(k_buf_src);
	return err;
}

static int qcedev_sha_update(struct qcedev_async_req *qcedev_areq,
				struct qcedev_handle *handle,
				struct scatterlist *sg_src)
{
	int err = 0;
	int i = 0;
	int j = 0;
	int k = 0;
	int num_entries = 0;
	uint32_t total = 0;

	if (!handle->sha_ctxt.init_done) {
		pr_err("%s Init was not called\n", __func__);
		return -EINVAL;
	}

	if (qcedev_areq->sha_op_req.data_len > QCE_MAX_OPER_DATA) {

		struct	qcedev_sha_op_req *saved_req;
		struct	qcedev_sha_op_req req;
		struct	qcedev_sha_op_req *sreq = &qcedev_areq->sha_op_req;

		/* save the original req structure */
		saved_req =
			kmalloc(sizeof(struct qcedev_sha_op_req), GFP_KERNEL);
		if (saved_req == NULL) {
			pr_err("%s:Can't Allocate mem:saved_req 0x%lx\n",
						__func__, (uintptr_t)saved_req);
			return -ENOMEM;
		}
		memcpy(&req, sreq, sizeof(struct qcedev_sha_op_req));
		memcpy(saved_req, sreq, sizeof(struct qcedev_sha_op_req));

		i = 0;
		/* Address 32 KB  at a time */
		while ((i < req.entries) && (err == 0)) {
			if (sreq->data[i].len > QCE_MAX_OPER_DATA) {
				sreq->data[0].len = QCE_MAX_OPER_DATA;
				if (i > 0) {
					sreq->data[0].vaddr =
							sreq->data[i].vaddr;
				}

				sreq->data_len = QCE_MAX_OPER_DATA;
				sreq->entries = 1;

				err = qcedev_sha_update_max_xfer(qcedev_areq,
								handle, sg_src);

				sreq->data[i].len = req.data[i].len -
							QCE_MAX_OPER_DATA;
				sreq->data[i].vaddr = req.data[i].vaddr +
							QCE_MAX_OPER_DATA;
				req.data[i].vaddr = sreq->data[i].vaddr;
				req.data[i].len = sreq->data[i].len;
			} else {
				total = 0;
				for (j = i; j < req.entries; j++) {
					num_entries++;
					if ((total + sreq->data[j].len) >=
							QCE_MAX_OPER_DATA) {
						sreq->data[j].len =
						(QCE_MAX_OPER_DATA - total);
						total = QCE_MAX_OPER_DATA;
						break;
					}
					total += sreq->data[j].len;
				}

				sreq->data_len = total;
				if (i > 0)
					for (k = 0; k < num_entries; k++) {
						sreq->data[k].len =
							sreq->data[i+k].len;
						sreq->data[k].vaddr =
							sreq->data[i+k].vaddr;
					}
				sreq->entries = num_entries;

				i = j;
				err = qcedev_sha_update_max_xfer(qcedev_areq,
								handle, sg_src);
				num_entries = 0;

				sreq->data[i].vaddr = req.data[i].vaddr +
							sreq->data[i].len;
				sreq->data[i].len = req.data[i].len -
							sreq->data[i].len;
				req.data[i].vaddr = sreq->data[i].vaddr;
				req.data[i].len = sreq->data[i].len;

				if (sreq->data[i].len == 0)
					i++;
			}
		} /* end of while ((i < req.entries) && (err == 0)) */

		/* Restore the original req structure */
		for (i = 0; i < saved_req->entries; i++) {
			sreq->data[i].len = saved_req->data[i].len;
			sreq->data[i].vaddr = saved_req->data[i].vaddr;
		}
		sreq->entries = saved_req->entries;
		sreq->data_len = saved_req->data_len;
		memset(saved_req, 0, ksize((void *)saved_req));
		kfree(saved_req);
	} else
		err = qcedev_sha_update_max_xfer(qcedev_areq, handle, sg_src);

	return err;
}

static int qcedev_sha_final(struct qcedev_async_req *qcedev_areq,
				struct qcedev_handle *handle)
{
	int err = 0;
	struct scatterlist sg_src;
	uint32_t total;
	uint8_t *k_buf_src = NULL;
	uint8_t *k_align_src = NULL;

	if (!handle->sha_ctxt.init_done) {
		pr_err("%s Init was not called\n", __func__);
		return -EINVAL;
	}

	handle->sha_ctxt.last_blk = 1;

	total = handle->sha_ctxt.trailing_buf_len;

	k_buf_src = kmalloc(total + CACHE_LINE_SIZE * 2,
				GFP_KERNEL);
	if (k_buf_src == NULL)
		return -ENOMEM;

	k_align_src = (uint8_t *)ALIGN(((uintptr_t)k_buf_src),
						CACHE_LINE_SIZE);
	memcpy(k_align_src, &handle->sha_ctxt.trailing_buf[0], total);

	qcedev_areq->sha_req.sreq.src = (struct scatterlist *) &sg_src;

	sg_init_one(qcedev_areq->sha_req.sreq.src, k_align_src, total);

	qcedev_areq->sha_req.sreq.nbytes = total;

	err = submit_req(qcedev_areq, handle);

	handle->sha_ctxt.first_blk = 0;
	handle->sha_ctxt.last_blk = 0;
	handle->sha_ctxt.auth_data[0] = 0;
	handle->sha_ctxt.auth_data[1] = 0;
	handle->sha_ctxt.trailing_buf_len = 0;
	handle->sha_ctxt.init_done = false;
	memset(&handle->sha_ctxt.trailing_buf[0], 0, 64);
	memset(k_buf_src, 0, ksize((void *)k_buf_src));
	kfree(k_buf_src);
	qcedev_areq->sha_req.sreq.src = NULL;
	return err;
}

static int qcedev_hash_cmac(struct qcedev_async_req *qcedev_areq,
					struct qcedev_handle *handle,
					struct scatterlist *sg_src)
{
	int err = 0;
	int i = 0;
	uint32_t total;

	uint8_t *user_src = NULL;
	uint8_t *k_src = NULL;
	uint8_t *k_buf_src = NULL;

	total = qcedev_areq->sha_op_req.data_len;

	if ((qcedev_areq->sha_op_req.authklen != QCEDEV_AES_KEY_128) &&
		(qcedev_areq->sha_op_req.authklen != QCEDEV_AES_KEY_256)) {
		pr_err("%s: unsupported key length\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(&handle->sha_ctxt.authkey[0],
				(void __user *)qcedev_areq->sha_op_req.authkey,
				qcedev_areq->sha_op_req.authklen))
		return -EFAULT;

	if (total > U32_MAX - CACHE_LINE_SIZE * 2)
		return -EINVAL;

	k_buf_src = kmalloc(total + CACHE_LINE_SIZE * 2, GFP_KERNEL);
	if (k_buf_src == NULL)
		return -ENOMEM;

	k_src = k_buf_src;

	/* Copy data from user src(s) */
	user_src = qcedev_areq->sha_op_req.data[0].vaddr;
	for (i = 0; i < qcedev_areq->sha_op_req.entries; i++) {
		user_src = qcedev_areq->sha_op_req.data[i].vaddr;
		if (user_src && copy_from_user(k_src, (void __user *)user_src,
				qcedev_areq->sha_op_req.data[i].len)) {
			memset(k_buf_src, 0, ksize((void *)k_buf_src));
			kfree(k_buf_src);
			return -EFAULT;
		}
		k_src += qcedev_areq->sha_op_req.data[i].len;
	}

	qcedev_areq->sha_req.sreq.src = sg_src;
	sg_init_one(qcedev_areq->sha_req.sreq.src, k_buf_src, total);

	qcedev_areq->sha_req.sreq.nbytes = total;
	handle->sha_ctxt.diglen = qcedev_areq->sha_op_req.diglen;
	err = submit_req(qcedev_areq, handle);

	memset(k_buf_src, 0, ksize((void *)k_buf_src));
	kfree(k_buf_src);
	return err;
}

static int qcedev_set_hmac_auth_key(struct qcedev_async_req *areq,
					struct qcedev_handle *handle,
					struct scatterlist *sg_src)
{
	int err = 0;

	if (areq->sha_op_req.authklen <= QCEDEV_MAX_KEY_SIZE) {
		qcedev_sha_init(areq, handle);
		if (copy_from_user(&handle->sha_ctxt.authkey[0],
				(void __user *)areq->sha_op_req.authkey,
				areq->sha_op_req.authklen))
			return -EFAULT;
	} else {
		struct qcedev_async_req authkey_areq;
		uint8_t	authkey[QCEDEV_MAX_SHA_BLOCK_SIZE];

		init_completion(&authkey_areq.complete);

		authkey_areq.sha_op_req.entries = 1;
		authkey_areq.sha_op_req.data[0].vaddr =
						areq->sha_op_req.authkey;
		authkey_areq.sha_op_req.data[0].len = areq->sha_op_req.authklen;
		authkey_areq.sha_op_req.data_len = areq->sha_op_req.authklen;
		authkey_areq.sha_op_req.diglen = 0;
		authkey_areq.handle = handle;

		memset(&authkey_areq.sha_op_req.digest[0], 0,
						QCEDEV_MAX_SHA_DIGEST);
		if (areq->sha_op_req.alg == QCEDEV_ALG_SHA1_HMAC)
			authkey_areq.sha_op_req.alg = QCEDEV_ALG_SHA1;
		if (areq->sha_op_req.alg == QCEDEV_ALG_SHA256_HMAC)
			authkey_areq.sha_op_req.alg = QCEDEV_ALG_SHA256;

		authkey_areq.op_type = QCEDEV_CRYPTO_OPER_SHA;

		qcedev_sha_init(&authkey_areq, handle);
		err = qcedev_sha_update(&authkey_areq, handle, sg_src);
		if (!err)
			err = qcedev_sha_final(&authkey_areq, handle);
		else
			return err;
		memcpy(&authkey[0], &handle->sha_ctxt.digest[0],
				handle->sha_ctxt.diglen);
		qcedev_sha_init(areq, handle);

		memcpy(&handle->sha_ctxt.authkey[0], &authkey[0],
				handle->sha_ctxt.diglen);
	}
	return err;
}

static int qcedev_hmac_get_ohash(struct qcedev_async_req *qcedev_areq,
				struct qcedev_handle *handle)
{
	int err = 0;
	struct scatterlist sg_src;
	uint8_t *k_src = NULL;
	uint32_t sha_block_size = 0;
	uint32_t sha_digest_size = 0;

	if (qcedev_areq->sha_op_req.alg == QCEDEV_ALG_SHA1_HMAC) {
		sha_digest_size = SHA1_DIGEST_SIZE;
		sha_block_size = SHA1_BLOCK_SIZE;
	} else {
		if (qcedev_areq->sha_op_req.alg == QCEDEV_ALG_SHA256_HMAC) {
			sha_digest_size = SHA256_DIGEST_SIZE;
			sha_block_size = SHA256_BLOCK_SIZE;
		}
	}
	k_src = kmalloc(sha_block_size, GFP_KERNEL);
	if (k_src == NULL)
		return -ENOMEM;

	/* check for trailing buffer from previous updates and append it */
	memcpy(k_src, &handle->sha_ctxt.trailing_buf[0],
			handle->sha_ctxt.trailing_buf_len);

	qcedev_areq->sha_req.sreq.src = (struct scatterlist *) &sg_src;
	sg_init_one(qcedev_areq->sha_req.sreq.src, k_src, sha_block_size);

	qcedev_areq->sha_req.sreq.nbytes = sha_block_size;
	memset(&handle->sha_ctxt.trailing_buf[0], 0, sha_block_size);
	memcpy(&handle->sha_ctxt.trailing_buf[0], &handle->sha_ctxt.digest[0],
					sha_digest_size);
	handle->sha_ctxt.trailing_buf_len = sha_digest_size;

	handle->sha_ctxt.first_blk = 1;
	handle->sha_ctxt.last_blk = 0;
	handle->sha_ctxt.auth_data[0] = 0;
	handle->sha_ctxt.auth_data[1] = 0;

	if (qcedev_areq->sha_op_req.alg == QCEDEV_ALG_SHA1_HMAC) {
		memcpy(&handle->sha_ctxt.digest[0],
			&_std_init_vector_sha1_uint8[0], SHA1_DIGEST_SIZE);
		handle->sha_ctxt.diglen = SHA1_DIGEST_SIZE;
	}

	if (qcedev_areq->sha_op_req.alg == QCEDEV_ALG_SHA256_HMAC) {
		memcpy(&handle->sha_ctxt.digest[0],
			&_std_init_vector_sha256_uint8[0], SHA256_DIGEST_SIZE);
		handle->sha_ctxt.diglen = SHA256_DIGEST_SIZE;
	}
	err = submit_req(qcedev_areq, handle);

	handle->sha_ctxt.last_blk = 0;
	handle->sha_ctxt.first_blk = 0;
	memset(k_src, 0, ksize((void *)k_src));
	kfree(k_src);
	qcedev_areq->sha_req.sreq.src = NULL;
	return err;
}

static int qcedev_hmac_update_iokey(struct qcedev_async_req *areq,
				struct qcedev_handle *handle, bool ikey)
{
	int i;
	uint32_t constant;
	uint32_t sha_block_size;

	if (ikey)
		constant = 0x36;
	else
		constant = 0x5c;

	if (areq->sha_op_req.alg == QCEDEV_ALG_SHA1_HMAC)
		sha_block_size = SHA1_BLOCK_SIZE;
	else
		sha_block_size = SHA256_BLOCK_SIZE;

	memset(&handle->sha_ctxt.trailing_buf[0], 0, sha_block_size);
	for (i = 0; i < sha_block_size; i++)
		handle->sha_ctxt.trailing_buf[i] =
				(handle->sha_ctxt.authkey[i] ^ constant);

	handle->sha_ctxt.trailing_buf_len = sha_block_size;
	return 0;
}

static int qcedev_hmac_init(struct qcedev_async_req *areq,
				struct qcedev_handle *handle,
				struct scatterlist *sg_src)
{
	int err;
	struct qcedev_control *podev = handle->cntl;

	err = qcedev_set_hmac_auth_key(areq, handle, sg_src);
	if (err)
		return err;
	if (!podev->ce_support.sha_hmac)
		qcedev_hmac_update_iokey(areq, handle, true);
	return 0;
}

static int qcedev_hmac_final(struct qcedev_async_req *areq,
				struct qcedev_handle *handle)
{
	int err;
	struct qcedev_control *podev = handle->cntl;

	err = qcedev_sha_final(areq, handle);
	if (podev->ce_support.sha_hmac)
		return err;

	qcedev_hmac_update_iokey(areq, handle, false);
	err = qcedev_hmac_get_ohash(areq, handle);
	if (err)
		return err;
	err = qcedev_sha_final(areq, handle);

	return err;
}

static int qcedev_hash_init(struct qcedev_async_req *areq,
				struct qcedev_handle *handle,
				struct scatterlist *sg_src)
{
	if ((areq->sha_op_req.alg == QCEDEV_ALG_SHA1) ||
			(areq->sha_op_req.alg == QCEDEV_ALG_SHA256))
		return qcedev_sha_init(areq, handle);
	else
		return qcedev_hmac_init(areq, handle, sg_src);
}

static int qcedev_hash_update(struct qcedev_async_req *qcedev_areq,
				struct qcedev_handle *handle,
				struct scatterlist *sg_src)
{
	return qcedev_sha_update(qcedev_areq, handle, sg_src);
}

static int qcedev_hash_final(struct qcedev_async_req *areq,
				struct qcedev_handle *handle)
{
	if ((areq->sha_op_req.alg == QCEDEV_ALG_SHA1) ||
			(areq->sha_op_req.alg == QCEDEV_ALG_SHA256))
		return qcedev_sha_final(areq, handle);
	else
		return qcedev_hmac_final(areq, handle);
}

static int qcedev_vbuf_ablk_cipher_max_xfer(struct qcedev_async_req *areq,
				int *di, struct qcedev_handle *handle,
				uint8_t *k_align_src)
{
	int err = 0;
	int i = 0;
	int dst_i = *di;
	struct scatterlist sg_src;
	uint32_t byteoffset = 0;
	uint8_t *user_src = NULL;
	uint8_t *k_align_dst = k_align_src;
	struct	qcedev_cipher_op_req *creq = &areq->cipher_op_req;


	if (areq->cipher_op_req.mode == QCEDEV_AES_MODE_CTR)
		byteoffset = areq->cipher_op_req.byteoffset;

	user_src = areq->cipher_op_req.vbuf.src[0].vaddr;
	if (user_src && copy_from_user((k_align_src + byteoffset),
				(void __user *)user_src,
				areq->cipher_op_req.vbuf.src[0].len))
		return -EFAULT;

	k_align_src += byteoffset + areq->cipher_op_req.vbuf.src[0].len;

	for (i = 1; i < areq->cipher_op_req.entries; i++) {
		user_src = areq->cipher_op_req.vbuf.src[i].vaddr;
		if (user_src && copy_from_user(k_align_src,
					(void __user *)user_src,
					areq->cipher_op_req.vbuf.src[i].len)) {
			return -EFAULT;
		}
		k_align_src += areq->cipher_op_req.vbuf.src[i].len;
	}

	/* restore src beginning */
	k_align_src = k_align_dst;
	areq->cipher_op_req.data_len += byteoffset;

	areq->cipher_req.creq.src = (struct scatterlist *) &sg_src;
	areq->cipher_req.creq.dst = (struct scatterlist *) &sg_src;

	/* In place encryption/decryption */
	sg_init_one(areq->cipher_req.creq.src,
					k_align_dst,
					areq->cipher_op_req.data_len);

	areq->cipher_req.creq.cryptlen = areq->cipher_op_req.data_len;
	areq->cipher_req.creq.iv = areq->cipher_op_req.iv;
	areq->cipher_op_req.entries = 1;

	err = submit_req(areq, handle);

	/* copy data to destination buffer*/
	creq->data_len -= byteoffset;

	while (creq->data_len > 0) {
		if (creq->vbuf.dst[dst_i].len <= creq->data_len) {
			if (err == 0 && copy_to_user(
				(void __user *)creq->vbuf.dst[dst_i].vaddr,
					(k_align_dst + byteoffset),
					creq->vbuf.dst[dst_i].len)) {
				err = -EFAULT;
				goto exit;
			}

			k_align_dst += creq->vbuf.dst[dst_i].len;
			creq->data_len -= creq->vbuf.dst[dst_i].len;
			dst_i++;
		} else {
			if (err == 0 && copy_to_user(
				(void __user *)creq->vbuf.dst[dst_i].vaddr,
					(k_align_dst + byteoffset),
				creq->data_len)) {
				err = -EFAULT;
				goto exit;
			}

			k_align_dst += creq->data_len;
			creq->vbuf.dst[dst_i].len -= creq->data_len;
			creq->vbuf.dst[dst_i].vaddr += creq->data_len;
			creq->data_len = 0;
		}
	}
	*di = dst_i;
exit:
	areq->cipher_req.creq.src = NULL;
	areq->cipher_req.creq.dst = NULL;
	return err;
};

static int qcedev_vbuf_ablk_cipher(struct qcedev_async_req *areq,
						struct qcedev_handle *handle)
{
	int err = 0;
	int di = 0;
	int i = 0;
	int j = 0;
	int k = 0;
	uint32_t byteoffset = 0;
	int num_entries = 0;
	uint32_t total = 0;
	uint32_t len;
	uint8_t *k_buf_src = NULL;
	uint8_t *k_align_src = NULL;
	uint32_t max_data_xfer;
	struct qcedev_cipher_op_req *saved_req;
	struct	qcedev_cipher_op_req *creq = &areq->cipher_op_req;

	total = 0;

	if (areq->cipher_op_req.mode == QCEDEV_AES_MODE_CTR)
		byteoffset = areq->cipher_op_req.byteoffset;
	k_buf_src = kmalloc(QCE_MAX_OPER_DATA + CACHE_LINE_SIZE * 2,
				GFP_KERNEL);
	if (k_buf_src == NULL)
		return -ENOMEM;
	k_align_src = (uint8_t *)ALIGN(((uintptr_t)k_buf_src),
							CACHE_LINE_SIZE);
	max_data_xfer = QCE_MAX_OPER_DATA - byteoffset;

	saved_req = kmemdup(creq, sizeof(struct qcedev_cipher_op_req),
				GFP_KERNEL);
	if (saved_req == NULL) {
		memset(k_buf_src, 0, ksize((void *)k_buf_src));
		kfree(k_buf_src);
		return -ENOMEM;

	}

	if (areq->cipher_op_req.data_len > max_data_xfer) {
		struct qcedev_cipher_op_req req;

		/* save the original req structure */
		memcpy(&req, creq, sizeof(struct qcedev_cipher_op_req));

		i = 0;
		/* Address 32 KB  at a time */
		while ((i < req.entries) && (err == 0)) {
			if (creq->vbuf.src[i].len > max_data_xfer) {
				creq->vbuf.src[0].len =	max_data_xfer;
				if (i > 0) {
					creq->vbuf.src[0].vaddr =
						creq->vbuf.src[i].vaddr;
				}

				creq->data_len = max_data_xfer;
				creq->entries = 1;

				err = qcedev_vbuf_ablk_cipher_max_xfer(areq,
						&di, handle, k_align_src);
				if (err < 0) {
					memset(saved_req, 0,
						ksize((void *)saved_req));
					memset(k_buf_src, 0,
						ksize((void *)k_buf_src));
					kfree(k_buf_src);
					kfree(saved_req);
					return err;
				}

				creq->vbuf.src[i].len =	req.vbuf.src[i].len -
							max_data_xfer;
				creq->vbuf.src[i].vaddr =
						req.vbuf.src[i].vaddr +
						max_data_xfer;
				req.vbuf.src[i].vaddr =
						creq->vbuf.src[i].vaddr;
				req.vbuf.src[i].len = creq->vbuf.src[i].len;

			} else {
				total = areq->cipher_op_req.byteoffset;
				for (j = i; j < req.entries; j++) {
					num_entries++;
					if ((total + creq->vbuf.src[j].len)
							>= max_data_xfer) {
						creq->vbuf.src[j].len =
						max_data_xfer - total;
						total = max_data_xfer;
						break;
					}
					total += creq->vbuf.src[j].len;
				}

				creq->data_len = total;
				if (i > 0)
					for (k = 0; k < num_entries; k++) {
						creq->vbuf.src[k].len =
						creq->vbuf.src[i+k].len;
						creq->vbuf.src[k].vaddr =
						creq->vbuf.src[i+k].vaddr;
					}
				creq->entries =  num_entries;

				i = j;
				err = qcedev_vbuf_ablk_cipher_max_xfer(areq,
						&di, handle, k_align_src);
				if (err < 0) {
					memset(saved_req, 0,
						ksize((void *)saved_req));
					memset(k_buf_src, 0,
						ksize((void *)k_buf_src));
					kfree(k_buf_src);
					kfree(saved_req);
					return err;
				}

				num_entries = 0;
				areq->cipher_op_req.byteoffset = 0;

				creq->vbuf.src[i].vaddr = req.vbuf.src[i].vaddr
					+ creq->vbuf.src[i].len;
				creq->vbuf.src[i].len =	req.vbuf.src[i].len -
							creq->vbuf.src[i].len;

				req.vbuf.src[i].vaddr =
						creq->vbuf.src[i].vaddr;
				req.vbuf.src[i].len = creq->vbuf.src[i].len;

				if (creq->vbuf.src[i].len == 0)
					i++;
			}

			areq->cipher_op_req.byteoffset = 0;
			max_data_xfer = QCE_MAX_OPER_DATA;
			byteoffset = 0;

		} /* end of while ((i < req.entries) && (err == 0)) */
	} else
		err = qcedev_vbuf_ablk_cipher_max_xfer(areq, &di, handle,
								k_align_src);

	/* Restore the original req structure */
	for (i = 0; i < saved_req->entries; i++) {
		creq->vbuf.src[i].len = saved_req->vbuf.src[i].len;
		creq->vbuf.src[i].vaddr = saved_req->vbuf.src[i].vaddr;
	}
	for (len = 0, i = 0; len < saved_req->data_len; i++) {
		creq->vbuf.dst[i].len = saved_req->vbuf.dst[i].len;
		creq->vbuf.dst[i].vaddr = saved_req->vbuf.dst[i].vaddr;
		len += saved_req->vbuf.dst[i].len;
	}
	creq->entries = saved_req->entries;
	creq->data_len = saved_req->data_len;
	creq->byteoffset = saved_req->byteoffset;

	memset(saved_req, 0, ksize((void *)saved_req));
	memset(k_buf_src, 0, ksize((void *)k_buf_src));
	kfree(saved_req);
	kfree(k_buf_src);
	return err;

}

static int qcedev_smmu_ablk_offload_cipher(struct qcedev_async_req *areq,
				       struct qcedev_handle *handle)
{
	int i = 0;
	int err = 0;
	size_t byteoffset = 0;
	size_t transfer_data_len = 0;
	size_t pending_data_len = 0;
	size_t max_data_xfer = MAX_CEHW_REQ_TRANSFER_SIZE - byteoffset;
	uint8_t *user_src = NULL;
	uint8_t *user_dst = NULL;
	struct scatterlist sg_src;
	struct scatterlist sg_dst;

	if (areq->offload_cipher_op_req.mode == QCEDEV_AES_MODE_CTR)
		byteoffset = areq->offload_cipher_op_req.byteoffset;

	/*
	 * areq has two components:
	 *      a) Request that comes from userspace i.e. offload_cipher_op_req
	 *      b) Request that QCE understands - skcipher i.e. cipher_req.creq
	 *         skcipher has sglist pointers src and dest that would carry
	 *         data to/from CE.
	 */
	areq->cipher_req.creq.src = &sg_src;
	areq->cipher_req.creq.dst = &sg_dst;
	sg_init_table(&sg_src, 1);
	sg_init_table(&sg_dst, 1);

	for (i = 0; i < areq->offload_cipher_op_req.entries; i++) {
		transfer_data_len = 0;
		pending_data_len = areq->offload_cipher_op_req.vbuf.src[i].len;
		user_src = areq->offload_cipher_op_req.vbuf.src[i].vaddr;
		user_src += byteoffset;

		user_dst = areq->offload_cipher_op_req.vbuf.dst[i].vaddr;
		user_dst += byteoffset;

		areq->cipher_req.creq.iv = areq->offload_cipher_op_req.iv;

		while (pending_data_len) {
			transfer_data_len = min(max_data_xfer,
						pending_data_len);
			sg_src.dma_address = (dma_addr_t)user_src;
			sg_dst.dma_address = (dma_addr_t)user_dst;
			areq->cipher_req.creq.cryptlen = transfer_data_len;

			sg_src.length = transfer_data_len;
			sg_dst.length = transfer_data_len;

			err = submit_req(areq, handle);
			if (err) {
				pr_err("%s: Error processing req, err = %d\n",
						__func__, err);
				goto exit;
			}
			/* update data len to be processed */
			pending_data_len -= transfer_data_len;
			user_src += transfer_data_len;
			user_dst += transfer_data_len;
		}
	}
exit:
	return err;
}

static int qcedev_check_cipher_key(struct qcedev_cipher_op_req *req,
						struct qcedev_control *podev)
{
	/* if intending to use HW key make sure key fields are set
	 * correctly and HW key is indeed supported in target
	 */
	if (req->encklen == 0) {
		int i;

		for (i = 0; i < QCEDEV_MAX_KEY_SIZE; i++) {
			if (req->enckey[i]) {
				pr_err("%s: Invalid key: non-zero key input\n",
								__func__);
				goto error;
			}
		}
		if ((req->op != QCEDEV_OPER_ENC_NO_KEY) &&
			(req->op != QCEDEV_OPER_DEC_NO_KEY))
			if (!podev->platform_support.hw_key_support) {
				pr_err("%s: Invalid op %d\n", __func__,
						(uint32_t)req->op);
				goto error;
			}
	} else {
		if (req->encklen == QCEDEV_AES_KEY_192) {
			if (!podev->ce_support.aes_key_192) {
				pr_err("%s: AES-192 not supported\n", __func__);
				goto error;
			}
		} else {
			/* if not using HW key make sure key
			 * length is valid
			 */
			if (req->mode == QCEDEV_AES_MODE_XTS) {
				if ((req->encklen != QCEDEV_AES_KEY_128*2) &&
				(req->encklen != QCEDEV_AES_KEY_256*2)) {
					pr_err("%s: unsupported key size: %d\n",
							__func__, req->encklen);
					goto error;
				}
			} else {
				if ((req->encklen != QCEDEV_AES_KEY_128) &&
					(req->encklen != QCEDEV_AES_KEY_256)) {
					pr_err("%s: unsupported key size %d\n",
							__func__, req->encklen);
					goto error;
				}
			}
		}
	}
	return 0;
error:
	return -EINVAL;
}

static int qcedev_check_cipher_params(struct qcedev_cipher_op_req *req,
						struct qcedev_control *podev)
{
	uint32_t total = 0;
	uint32_t i;

	if (req->use_pmem) {
		pr_err("%s: Use of PMEM is not supported\n", __func__);
		goto error;
	}
	if ((req->entries == 0) || (req->data_len == 0) ||
			(req->entries > QCEDEV_MAX_BUFFERS)) {
		pr_err("%s: Invalid cipher length/entries\n", __func__);
		goto error;
	}
	if ((req->alg >= QCEDEV_ALG_LAST) ||
		(req->mode >= QCEDEV_AES_DES_MODE_LAST)) {
		pr_err("%s: Invalid algorithm %d\n", __func__,
						(uint32_t)req->alg);
		goto error;
	}
	if ((req->mode == QCEDEV_AES_MODE_XTS) &&
				(!podev->ce_support.aes_xts)) {
		pr_err("%s: XTS algorithm is not supported\n", __func__);
		goto error;
	}
	if (req->alg == QCEDEV_ALG_AES) {
		if (qcedev_check_cipher_key(req, podev))
			goto error;

	}
	/* if using a byteoffset, make sure it is CTR mode using vbuf */
	if (req->byteoffset) {
		if (req->mode != QCEDEV_AES_MODE_CTR) {
			pr_err("%s: Operation on byte offset not supported\n",
								 __func__);
			goto error;
		}
		if (req->byteoffset >= AES_CE_BLOCK_SIZE) {
			pr_err("%s: Invalid byte offset\n", __func__);
			goto error;
		}
		total = req->byteoffset;
		for (i = 0; i < req->entries; i++) {
			if (total > U32_MAX - req->vbuf.src[i].len) {
				pr_err("%s:Integer overflow on total src len\n",
					__func__);
				goto error;
			}
			total += req->vbuf.src[i].len;
		}
	}

	if (req->data_len < req->byteoffset) {
		pr_err("%s: req data length %u is less than byteoffset %u\n",
				__func__, req->data_len, req->byteoffset);
		goto error;
	}

	/* Ensure IV size */
	if (req->ivlen > QCEDEV_MAX_IV_SIZE) {
		pr_err("%s: ivlen is not correct: %u\n", __func__, req->ivlen);
		goto error;
	}

	/* Ensure Key size */
	if (req->encklen > QCEDEV_MAX_KEY_SIZE) {
		pr_err("%s: Klen is not correct: %u\n", __func__, req->encklen);
		goto error;
	}

	/* Ensure zer ivlen for ECB  mode  */
	if (req->ivlen > 0) {
		if ((req->mode == QCEDEV_AES_MODE_ECB) ||
				(req->mode == QCEDEV_DES_MODE_ECB)) {
			pr_err("%s: Expecting a zero length IV\n", __func__);
			goto error;
		}
	} else {
		if ((req->mode != QCEDEV_AES_MODE_ECB) &&
				(req->mode != QCEDEV_DES_MODE_ECB)) {
			pr_err("%s: Expecting a non-zero ength IV\n", __func__);
			goto error;
		}
	}
	/* Check for sum of all dst length is equal to data_len  */
	for (i = 0, total = 0; i < req->entries; i++) {
		if (!req->vbuf.dst[i].vaddr && req->vbuf.dst[i].len) {
			pr_err("%s: NULL req dst vbuf[%d] with length %d\n",
				__func__, i, req->vbuf.dst[i].len);
			goto error;
		}
		if (req->vbuf.dst[i].len >= U32_MAX - total) {
			pr_err("%s: Integer overflow on total req dst vbuf length\n",
				__func__);
			goto error;
		}
		total += req->vbuf.dst[i].len;
	}
	if (total != req->data_len) {
		pr_err("%s: Total (i=%d) dst(%d) buf size != data_len (%d)\n",
			__func__, i, total, req->data_len);
		goto error;
	}
	/* Check for sum of all src length is equal to data_len  */
	for (i = 0, total = 0; i < req->entries; i++) {
		if (!req->vbuf.src[i].vaddr && req->vbuf.src[i].len) {
			pr_err("%s: NULL req src vbuf[%d] with length %d\n",
				__func__, i, req->vbuf.src[i].len);
			goto error;
		}
		if (req->vbuf.src[i].len > U32_MAX - total) {
			pr_err("%s: Integer overflow on total req src vbuf length\n",
				__func__);
			goto error;
		}
		total += req->vbuf.src[i].len;
	}
	if (total != req->data_len) {
		pr_err("%s: Total src(%d) buf size != data_len (%d)\n",
			__func__, total, req->data_len);
		goto error;
	}
	return 0;
error:
	return -EINVAL;

}

static int qcedev_check_sha_params(struct qcedev_sha_op_req *req,
						struct qcedev_control *podev)
{
	uint32_t total = 0;
	uint32_t i;

	if ((req->alg == QCEDEV_ALG_AES_CMAC) &&
				(!podev->ce_support.cmac)) {
		pr_err("%s: CMAC not supported\n", __func__);
		goto sha_error;
	}
	if ((!req->entries) || (req->entries > QCEDEV_MAX_BUFFERS)) {
		pr_err("%s: Invalid num entries (%d)\n",
						__func__, req->entries);
		goto sha_error;
	}

	if (req->alg >= QCEDEV_ALG_SHA_ALG_LAST) {
		pr_err("%s: Invalid algorithm (%d)\n", __func__, req->alg);
		goto sha_error;
	}
	if ((req->alg == QCEDEV_ALG_SHA1_HMAC) ||
			(req->alg == QCEDEV_ALG_SHA256_HMAC)) {
		if (req->authkey == NULL) {
			pr_err("%s: Invalid authkey pointer\n", __func__);
			goto sha_error;
		}
		if (req->authklen <= 0) {
			pr_err("%s: Invalid authkey length (%d)\n",
						__func__, req->authklen);
			goto sha_error;
		}
	}

	if (req->alg == QCEDEV_ALG_AES_CMAC) {
		if ((req->authklen != QCEDEV_AES_KEY_128) &&
					(req->authklen != QCEDEV_AES_KEY_256)) {
			pr_err("%s: unsupported key length\n", __func__);
			goto sha_error;
		}
	}

	/* Check for sum of all src length is equal to data_len  */
	for (i = 0, total = 0; i < req->entries; i++) {
		if (req->data[i].len > U32_MAX - total) {
			pr_err("%s: Integer overflow on total req buf length\n",
				__func__);
			goto sha_error;
		}
		total += req->data[i].len;
	}

	if (total != req->data_len) {
		pr_err("%s: Total src(%d) buf size != data_len (%d)\n",
			__func__, total, req->data_len);
		goto sha_error;
	}
	return 0;
sha_error:
	return -EINVAL;
}

static int qcedev_check_offload_cipher_key(struct qcedev_offload_cipher_op_req *req,
				       struct qcedev_control *podev)
{
	if (req->encklen == 0)
		return -EINVAL;

	/* AES-192 is not a valid option for OFFLOAD use case */
	if ((req->encklen != QCEDEV_AES_KEY_128) &&
			(req->encklen != QCEDEV_AES_KEY_256)) {
		pr_err("%s: unsupported key size %d\n",
					__func__, req->encklen);
		goto error;
	}

	return 0;
error:
	return -EINVAL;
}

static int qcedev_check_offload_cipher_params(struct qcedev_offload_cipher_op_req *req,
					  struct qcedev_control *podev)
{
	uint32_t total = 0;
	int i = 0;

	if ((req->entries == 0) || (req->data_len == 0) ||
		(req->entries > QCEDEV_MAX_BUFFERS)) {
		pr_err("%s: Invalid cipher length/entries\n", __func__);
		goto error;
	}

	if ((req->alg != QCEDEV_ALG_AES) ||
		(req->mode > QCEDEV_AES_MODE_CTR)) {
		pr_err("%s: Invalid algorithm %d\n", __func__,
					(uint32_t)req->alg);
		goto error;
	}

	if (qcedev_check_offload_cipher_key(req, podev))
		goto error;

	if (req->block_offset >= AES_CE_BLOCK_SIZE)
		goto error;

	/* if using a byteoffset, make sure it is CTR mode using vbuf */
	if (req->byteoffset) {
		if (req->mode != QCEDEV_AES_MODE_CTR) {
			pr_err("%s: Operation on byte offset not supported\n",
						__func__);
			goto error;
		}
		if (req->byteoffset >= AES_CE_BLOCK_SIZE) {
			pr_err("%s: Invalid byte offset\n", __func__);
			goto error;
		}
		total = req->byteoffset;
		for (i = 0; i < req->entries; i++) {
			if (total > U32_MAX - req->vbuf.src[i].len) {
				pr_err("%s:Int overflow on total src len\n",
						__func__);
				goto error;
			}
			total += req->vbuf.src[i].len;
		}
	}

	if (req->data_len < req->byteoffset) {
		pr_err("%s: req data length %u is less than byteoffset %u\n",
				__func__, req->data_len, req->byteoffset);
		goto error;
	}

	/* Ensure IV size */
	if (req->ivlen > QCEDEV_MAX_IV_SIZE) {
		pr_err("%s: ivlen is not correct: %u\n", __func__, req->ivlen);
		goto error;
	}

	/* Ensure Key size */
	if (req->encklen > QCEDEV_MAX_KEY_SIZE) {
		pr_err("%s: Klen is not correct: %u\n", __func__,
						req->encklen);
		goto error;
	}

	/* Check for sum of all dst length is equal to data_len  */
	for (i = 0, total = 0; i < req->entries; i++) {
		if (!req->vbuf.dst[i].vaddr && req->vbuf.dst[i].len) {
			pr_err("%s: NULL req dst vbuf[%d] with length %d\n",
					__func__, i, req->vbuf.dst[i].len);
			goto error;
		}
		if (req->vbuf.dst[i].len >= U32_MAX - total) {
			pr_err("%s: Int overflow on total req dst vbuf len\n",
					__func__);
			goto error;
		}
		total += req->vbuf.dst[i].len;
	}

	if (total != req->data_len) {
		pr_err("%s: Total (i=%d) dst(%d) buf size != data_len (%d)\n",
					__func__, i, total, req->data_len);
		goto error;
	}

	/* Check for sum of all src length is equal to data_len  */
	for (i = 0, total = 0; i < req->entries; i++) {
		if (!req->vbuf.src[i].vaddr && req->vbuf.src[i].len) {
			pr_err("%s: NULL req src vbuf[%d] with length %d\n",
					__func__, i, req->vbuf.src[i].len);
			goto error;
		}
		if (req->vbuf.src[i].len > U32_MAX - total) {
			pr_err("%s: Int overflow on total req src vbuf len\n",
					__func__);
			goto error;
		}
		total += req->vbuf.src[i].len;
	}

	if (total != req->data_len) {
		pr_err("%s: Total src(%d) buf size != data_len (%d)\n",
				__func__, total, req->data_len);
		goto error;
	}

	return 0;
error:
	return -EINVAL;
}

long qcedev_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct qcedev_handle *handle;
	struct qcedev_control *podev;
	struct qcedev_async_req *qcedev_areq;
	struct qcedev_stat *pstat;

	qcedev_areq = kzalloc(sizeof(struct qcedev_async_req), GFP_KERNEL);
	if (!qcedev_areq)
		return -ENOMEM;

	handle =  file->private_data;
	podev =  handle->cntl;
	qcedev_areq->handle = handle;
	if (podev == NULL || podev->magic != QCEDEV_MAGIC) {
		pr_err("%s: invalid handle %pK\n",
			__func__, podev);
		err = -ENOENT;
		goto exit_free_qcedev_areq;
	}

	/* Verify user arguments. */
	if (_IOC_TYPE(cmd) != QCEDEV_IOC_MAGIC) {
		err = -ENOTTY;
		goto exit_free_qcedev_areq;
	}

	init_completion(&qcedev_areq->complete);
	pstat = &_qcedev_stat;

	switch (cmd) {
	case QCEDEV_IOCTL_ENC_REQ:
	case QCEDEV_IOCTL_DEC_REQ:
		if (copy_from_user(&qcedev_areq->cipher_op_req,
				(void __user *)arg,
				sizeof(struct qcedev_cipher_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->op_type = QCEDEV_CRYPTO_OPER_CIPHER;

		if (qcedev_check_cipher_params(&qcedev_areq->cipher_op_req,
				podev)) {
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}

		err = qcedev_vbuf_ablk_cipher(qcedev_areq, handle);
		if (err)
			goto exit_free_qcedev_areq;
		if (copy_to_user((void __user *)arg,
					&qcedev_areq->cipher_op_req,
					sizeof(struct qcedev_cipher_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		break;

	case QCEDEV_IOCTL_OFFLOAD_OP_REQ:
		if (copy_from_user(&qcedev_areq->offload_cipher_op_req,
				(void __user *)arg,
				sizeof(struct qcedev_offload_cipher_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->op_type = QCEDEV_CRYPTO_OPER_OFFLOAD_CIPHER;
		if (qcedev_check_offload_cipher_params(
				&qcedev_areq->offload_cipher_op_req, podev)) {
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}

		err = qcedev_smmu_ablk_offload_cipher(qcedev_areq, handle);
		if (err)
			goto exit_free_qcedev_areq;

		if (copy_to_user((void __user *)arg,
				&qcedev_areq->offload_cipher_op_req,
				sizeof(struct qcedev_offload_cipher_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		break;

	case QCEDEV_IOCTL_SHA_INIT_REQ:
		{
		struct scatterlist sg_src;

		if (copy_from_user(&qcedev_areq->sha_op_req,
					(void __user *)arg,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		mutex_lock(&hash_access_lock);
		if (qcedev_check_sha_params(&qcedev_areq->sha_op_req, podev)) {
			mutex_unlock(&hash_access_lock);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->op_type = QCEDEV_CRYPTO_OPER_SHA;
		err = qcedev_hash_init(qcedev_areq, handle, &sg_src);
		if (err) {
			mutex_unlock(&hash_access_lock);
			goto exit_free_qcedev_areq;
		}
		mutex_unlock(&hash_access_lock);
		if (copy_to_user((void __user *)arg, &qcedev_areq->sha_op_req,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		handle->sha_ctxt.init_done = true;
		}
		break;
	case QCEDEV_IOCTL_GET_CMAC_REQ:
		if (!podev->ce_support.cmac) {
			err = -ENOTTY;
			goto exit_free_qcedev_areq;
		}
		/* Fall-through */
	case QCEDEV_IOCTL_SHA_UPDATE_REQ:
		{
		struct scatterlist sg_src;

		if (copy_from_user(&qcedev_areq->sha_op_req,
					(void __user *)arg,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		mutex_lock(&hash_access_lock);
		if (qcedev_check_sha_params(&qcedev_areq->sha_op_req, podev)) {
			mutex_unlock(&hash_access_lock);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->op_type = QCEDEV_CRYPTO_OPER_SHA;

		if (qcedev_areq->sha_op_req.alg == QCEDEV_ALG_AES_CMAC) {
			err = qcedev_hash_cmac(qcedev_areq, handle, &sg_src);
			if (err) {
				mutex_unlock(&hash_access_lock);
				goto exit_free_qcedev_areq;
			}
		} else {
			if (!handle->sha_ctxt.init_done) {
				pr_err("%s Init was not called\n", __func__);
				mutex_unlock(&hash_access_lock);
				err = -EINVAL;
				goto exit_free_qcedev_areq;
			}
			err = qcedev_hash_update(qcedev_areq, handle, &sg_src);
			if (err) {
				mutex_unlock(&hash_access_lock);
				goto exit_free_qcedev_areq;
			}
		}

		if (handle->sha_ctxt.diglen > QCEDEV_MAX_SHA_DIGEST) {
			pr_err("Invalid sha_ctxt.diglen %d\n",
					handle->sha_ctxt.diglen);
			mutex_unlock(&hash_access_lock);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		memcpy(&qcedev_areq->sha_op_req.digest[0],
				&handle->sha_ctxt.digest[0],
				handle->sha_ctxt.diglen);
		mutex_unlock(&hash_access_lock);
		if (copy_to_user((void __user *)arg, &qcedev_areq->sha_op_req,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		}
		break;

	case QCEDEV_IOCTL_SHA_FINAL_REQ:

		if (!handle->sha_ctxt.init_done) {
			pr_err("%s Init was not called\n", __func__);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		if (copy_from_user(&qcedev_areq->sha_op_req,
					(void __user *)arg,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		mutex_lock(&hash_access_lock);
		if (qcedev_check_sha_params(&qcedev_areq->sha_op_req, podev)) {
			mutex_unlock(&hash_access_lock);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->op_type = QCEDEV_CRYPTO_OPER_SHA;
		err = qcedev_hash_final(qcedev_areq, handle);
		if (err) {
			mutex_unlock(&hash_access_lock);
			goto exit_free_qcedev_areq;
		}
		if (handle->sha_ctxt.diglen > QCEDEV_MAX_SHA_DIGEST) {
			pr_err("Invalid sha_ctxt.diglen %d\n",
					handle->sha_ctxt.diglen);
			mutex_unlock(&hash_access_lock);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->sha_op_req.diglen = handle->sha_ctxt.diglen;
		memcpy(&qcedev_areq->sha_op_req.digest[0],
				&handle->sha_ctxt.digest[0],
				handle->sha_ctxt.diglen);
		mutex_unlock(&hash_access_lock);
		if (copy_to_user((void __user *)arg, &qcedev_areq->sha_op_req,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		handle->sha_ctxt.init_done = false;
		break;

	case QCEDEV_IOCTL_GET_SHA_REQ:
		{
		struct scatterlist sg_src;

		if (copy_from_user(&qcedev_areq->sha_op_req,
					(void __user *)arg,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		mutex_lock(&hash_access_lock);
		if (qcedev_check_sha_params(&qcedev_areq->sha_op_req, podev)) {
			mutex_unlock(&hash_access_lock);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->op_type = QCEDEV_CRYPTO_OPER_SHA;
		qcedev_hash_init(qcedev_areq, handle, &sg_src);
		err = qcedev_hash_update(qcedev_areq, handle, &sg_src);
		if (err) {
			mutex_unlock(&hash_access_lock);
			goto exit_free_qcedev_areq;
		}
		err = qcedev_hash_final(qcedev_areq, handle);
		if (err) {
			mutex_unlock(&hash_access_lock);
			goto exit_free_qcedev_areq;
		}
		if (handle->sha_ctxt.diglen > QCEDEV_MAX_SHA_DIGEST) {
			pr_err("Invalid sha_ctxt.diglen %d\n",
					handle->sha_ctxt.diglen);
			mutex_unlock(&hash_access_lock);
			err = -EINVAL;
			goto exit_free_qcedev_areq;
		}
		qcedev_areq->sha_op_req.diglen =	handle->sha_ctxt.diglen;
		memcpy(&qcedev_areq->sha_op_req.digest[0],
				&handle->sha_ctxt.digest[0],
				handle->sha_ctxt.diglen);
		mutex_unlock(&hash_access_lock);
		if (copy_to_user((void __user *)arg, &qcedev_areq->sha_op_req,
					sizeof(struct qcedev_sha_op_req))) {
			err = -EFAULT;
			goto exit_free_qcedev_areq;
		}
		}
		break;

	case QCEDEV_IOCTL_MAP_BUF_REQ:
		{
			unsigned long long vaddr = 0;
			struct qcedev_map_buf_req map_buf = { {0} };
			int i = 0;

			if (copy_from_user(&map_buf,
					(void __user *)arg, sizeof(map_buf))) {
				err = -EFAULT;
				goto exit_free_qcedev_areq;
			}

			if (map_buf.num_fds > QCEDEV_MAX_BUFFERS) {
				err = -EINVAL;
				goto exit_free_qcedev_areq;
			}

			for (i = 0; i < map_buf.num_fds; i++) {
				err = qcedev_check_and_map_buffer(handle,
						map_buf.fd[i],
						map_buf.fd_offset[i],
						map_buf.fd_size[i],
						&vaddr);
				if (err) {
					pr_err(
						"%s: err: failed to map fd(%d) - %d\n",
						__func__, map_buf.fd[i], err);
					goto exit_free_qcedev_areq;
				}
				map_buf.buf_vaddr[i] = vaddr;
				pr_info("%s: info: vaddr = %llx\n, fd = %d",
					__func__, vaddr, map_buf.fd[i]);
			}

			if (copy_to_user((void __user *)arg, &map_buf,
					sizeof(map_buf))) {
				err = -EFAULT;
				goto exit_free_qcedev_areq;
			}
			break;
		}

	case QCEDEV_IOCTL_UNMAP_BUF_REQ:
		{
			struct qcedev_unmap_buf_req unmap_buf = { { 0 } };
			int i = 0;

			if (copy_from_user(&unmap_buf,
				(void __user *)arg, sizeof(unmap_buf))) {
				err = -EFAULT;
				goto exit_free_qcedev_areq;
			}

			for (i = 0; i < unmap_buf.num_fds; i++) {
				err = qcedev_check_and_unmap_buffer(handle,
						unmap_buf.fd[i]);
				if (err) {
					pr_err(
						"%s: err: failed to unmap fd(%d) - %d\n",
						 __func__,
						unmap_buf.fd[i], err);
					goto exit_free_qcedev_areq;
				}
			}
			break;
		}

	default:
		err = -ENOTTY;
		goto exit_free_qcedev_areq;
	}

exit_free_qcedev_areq:
	kfree(qcedev_areq);
	return err;
}

static int qcedev_probe_device(struct platform_device *pdev)
{
	void *handle = NULL;
	int rc = 0;
	struct qcedev_control *podev;
	struct msm_ce_hw_support *platform_support;

	podev = &qce_dev[0];

	rc = alloc_chrdev_region(&qcedev_device_no, 0, 1, QCEDEV_DEV);
	if (rc < 0) {
		pr_err("alloc_chrdev_region failed %d\n", rc);
		return rc;
	}

	driver_class = class_create(THIS_MODULE, QCEDEV_DEV);
	if (IS_ERR(driver_class)) {
		rc = -ENOMEM;
		pr_err("class_create failed %d\n", rc);
		goto exit_unreg_chrdev_region;
	}

	class_dev = device_create(driver_class, NULL, qcedev_device_no, NULL,
			QCEDEV_DEV);
	if (IS_ERR(class_dev)) {
		pr_err("class_device_create failed %d\n", rc);
		rc = -ENOMEM;
		goto exit_destroy_class;
	}

	cdev_init(&podev->cdev, &qcedev_fops);
	podev->cdev.owner = THIS_MODULE;

	rc = cdev_add(&podev->cdev, MKDEV(MAJOR(qcedev_device_no), 0), 1);
	if (rc < 0) {
		pr_err("cdev_add failed %d\n", rc);
		goto exit_destroy_device;
	}
	podev->minor = 0;

	podev->high_bw_req_count = 0;
	INIT_LIST_HEAD(&podev->ready_commands);
	podev->active_command = NULL;

	INIT_LIST_HEAD(&podev->context_banks);

	spin_lock_init(&podev->lock);

	tasklet_init(&podev->done_tasklet, req_done, (unsigned long)podev);

	podev->icc_path = of_icc_get(&pdev->dev, "data_path");
	if (IS_ERR(podev->icc_path)) {
		rc = PTR_ERR(podev->icc_path);
		pr_err("%s Failed to get icc path with error %d\n",
			__func__, rc);
		goto exit_del_cdev;
	}

	rc = icc_set_bw(podev->icc_path, CRYPTO_AVG_BW, CRYPTO_PEAK_BW);
	if (rc) {
		pr_err("%s Unable to set high bandwidth\n", __func__);
		goto exit_unregister_bus_scale;
	}

	handle = qce_open(pdev, &rc);
	if (handle == NULL) {
		rc = -ENODEV;
		goto exit_scale_busbandwidth;
	}
	rc = icc_set_bw(podev->icc_path, 0, 0);
	if (rc) {
		pr_err("%s Unable to set to low bandwidth\n", __func__);
		goto exit_qce_close;
	}

	podev->qce = handle;
	podev->pdev = pdev;
	platform_set_drvdata(pdev, podev);

	qce_hw_support(podev->qce, &podev->ce_support);
	if (podev->ce_support.bam) {
		podev->platform_support.ce_shared = 0;
		podev->platform_support.shared_ce_resource = 0;
		podev->platform_support.hw_key_support =
						podev->ce_support.hw_key;
		podev->platform_support.sha_hmac = 1;
	} else {
		platform_support =
			(struct msm_ce_hw_support *)pdev->dev.platform_data;
		podev->platform_support.ce_shared = platform_support->ce_shared;
		podev->platform_support.shared_ce_resource =
				platform_support->shared_ce_resource;
		podev->platform_support.hw_key_support =
				platform_support->hw_key_support;
		podev->platform_support.sha_hmac = platform_support->sha_hmac;
	}

	podev->mem_client = qcedev_mem_new_client(MEM_ION);
	if (!podev->mem_client) {
		pr_err("%s: err: qcedev_mem_new_client failed\n", __func__);
		goto exit_qce_close;
	}

	rc = of_platform_populate(pdev->dev.of_node, qcedev_match,
			NULL, &pdev->dev);
	if (rc) {
		pr_err("%s: err: of_platform_populate failed: %d\n",
			__func__, rc);
		goto exit_mem_new_client;
	}

	return 0;

exit_mem_new_client:
	if (podev->mem_client)
		qcedev_mem_delete_client(podev->mem_client);
	podev->mem_client = NULL;

exit_qce_close:
	if (handle)
		qce_close(handle);
exit_scale_busbandwidth:
	icc_set_bw(podev->icc_path, 0, 0);
exit_unregister_bus_scale:
	if (podev->icc_path)
		icc_put(podev->icc_path);
exit_del_cdev:
	cdev_del(&podev->cdev);
exit_destroy_device:
	device_destroy(driver_class, qcedev_device_no);
exit_destroy_class:
	class_destroy(driver_class);
exit_unreg_chrdev_region:
	unregister_chrdev_region(qcedev_device_no, 1);

	podev->icc_path = NULL;
	platform_set_drvdata(pdev, NULL);
	podev->pdev = NULL;
	podev->qce = NULL;

	return rc;
}

static int qcedev_probe(struct platform_device *pdev)
{
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,qcedev"))
		return qcedev_probe_device(pdev);
	else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,qcedev,context-bank"))
		return qcedev_parse_context_bank(pdev);

	return -EINVAL;
};

static int qcedev_remove(struct platform_device *pdev)
{
	struct qcedev_control *podev;

	podev = platform_get_drvdata(pdev);
	if (!podev)
		return 0;
	if (podev->qce)
		qce_close(podev->qce);

	if (podev->icc_path)
		icc_put(podev->icc_path);
	tasklet_kill(&podev->done_tasklet);

	cdev_del(&podev->cdev);

	device_destroy(driver_class, qcedev_device_no);

	class_destroy(driver_class);

	unregister_chrdev_region(qcedev_device_no, 1);
	return 0;
};

static int qcedev_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct qcedev_control *podev;
	int ret;

	podev = platform_get_drvdata(pdev);

	if (!podev)
		return 0;

	mutex_lock(&qcedev_sent_bw_req);
	if (podev->high_bw_req_count) {
		ret = qcedev_control_clocks(podev, false);
		if (ret)
			goto suspend_exit;
	}

suspend_exit:
	mutex_unlock(&qcedev_sent_bw_req);
	return 0;
}

static int qcedev_resume(struct platform_device *pdev)
{
	struct qcedev_control *podev;
	int ret;

	podev = platform_get_drvdata(pdev);

	if (!podev)
		return 0;

	mutex_lock(&qcedev_sent_bw_req);
	if (podev->high_bw_req_count) {
		ret = qcedev_control_clocks(podev, true);
		if (ret)
			goto resume_exit;
	}

resume_exit:
	mutex_unlock(&qcedev_sent_bw_req);
	return 0;
}

static struct platform_driver qcedev_plat_driver = {
	.probe = qcedev_probe,
	.remove = qcedev_remove,
	.suspend = qcedev_suspend,
	.resume = qcedev_resume,
	.driver = {
		.name = "qce",
		.of_match_table = qcedev_match,
	},
};

static int _disp_stats(int id)
{
	struct qcedev_stat *pstat;
	int len = 0;

	pstat = &_qcedev_stat;
	len = scnprintf(_debug_read_buf, DEBUG_MAX_RW_BUF - 1,
			"\nQTI QCE dev driver %d Statistics:\n",
				id + 1);

	len += scnprintf(_debug_read_buf + len, DEBUG_MAX_RW_BUF - len - 1,
			"   Encryption operation success       : %d\n",
					pstat->qcedev_enc_success);
	len += scnprintf(_debug_read_buf + len, DEBUG_MAX_RW_BUF - len - 1,
			"   Encryption operation fail   : %d\n",
					pstat->qcedev_enc_fail);
	len += scnprintf(_debug_read_buf + len, DEBUG_MAX_RW_BUF - len - 1,
			"   Decryption operation success     : %d\n",
					pstat->qcedev_dec_success);

	len += scnprintf(_debug_read_buf + len, DEBUG_MAX_RW_BUF - len - 1,
			"   Encryption operation fail          : %d\n",
					pstat->qcedev_dec_fail);

	return len;
}

static ssize_t _debug_stats_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	ssize_t rc = -EINVAL;
	int qcedev = *((int *) file->private_data);
	int len;

	len = _disp_stats(qcedev);

	if (len <= count)
		rc = simple_read_from_buffer((void __user *) buf, len,
			ppos, (void *) _debug_read_buf, len);
	return rc;
}

static ssize_t _debug_stats_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	memset((char *)&_qcedev_stat, 0, sizeof(struct qcedev_stat));
	return count;
};

static const struct file_operations _debug_stats_ops = {
	.open =         simple_open,
	.read =         _debug_stats_read,
	.write =        _debug_stats_write,
};

static int _qcedev_debug_init(void)
{
	int rc;
	char name[DEBUG_MAX_FNAME];
	struct dentry *dent;

	_debug_dent = debugfs_create_dir("qcedev", NULL);
	if (IS_ERR(_debug_dent)) {
		pr_debug("qcedev debugfs_create_dir fail, error %ld\n",
				PTR_ERR(_debug_dent));
		return PTR_ERR(_debug_dent);
	}

	snprintf(name, DEBUG_MAX_FNAME-1, "stats-%d", 1);
	_debug_qcedev = 0;
	dent = debugfs_create_file(name, 0644, _debug_dent,
			&_debug_qcedev, &_debug_stats_ops);
	if (dent == NULL) {
		pr_debug("qcedev debugfs_create_file fail, error %ld\n",
				PTR_ERR(dent));
		rc = PTR_ERR(dent);
		goto err;
	}
	return 0;
err:
	debugfs_remove_recursive(_debug_dent);
	return rc;
}

static int qcedev_init(void)
{
	_qcedev_debug_init();
	return platform_driver_register(&qcedev_plat_driver);
}

static void qcedev_exit(void)
{
	debugfs_remove_recursive(_debug_dent);
	platform_driver_unregister(&qcedev_plat_driver);
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QTI DEV Crypto driver");

module_init(qcedev_init);
module_exit(qcedev_exit);
