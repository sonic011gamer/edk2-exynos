/*
 * (C) Copyright 2017 SAMSUNG Electronics
 * Kiwoong Kim <kwmad.kim@samsung.com>
 *
 * This software is proprietary of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or distributed, transmitted,
 * transcribed, stored in a retrieval system or translated into any human or computer language in any form by any means,
 * electronic, mechanical, manual or otherwise, or disclosed
 * to third parties without the express written permission of Samsung Electronics.
 *
 */


#include <lk/reg.h>
#include <stdlib.h>
#include <dev/ufs.h>
#include <platform.h>
#include <platform/vm.h>
#include <platform/ufs_provision.h>
#include <platform/delay.h>
#include <platform/ufs-dump.h>
#include <platform/chip_id.h>
#include <platform/mmu/mmu_func.h>
#include <target/target_api.h>
#include <target/board_info.h>
#include <dev/ufs_ops.h>
#ifdef UFS_CAL_DIR
#include <platform/ufs-vs-mmio.h>
#endif

#define	SCSI_MAX_INITIATOR	1
#define	SCSI_MAX_DEVICE		8
#undef	m_delay
#define m_delay(a) u_delay((a) * 1000)

u8 *wp_dat_buf;
scm *ptempscm;

/*
 * Multiple UFS host : cmd_scsi should be changed
 *Now only one host supported
 */
static struct ufs_host *_ufs[SCSI_MAX_INITIATOR] = { NULL, };
static int _ufs_curr_host = 0;
static int ufs_number_of_lus = 0;
static const char *ret_token[2] = {
	"PASS", "FAIL",
};

/*	Query Function		OPCODE				IDN				INDEX	SELECTOR	*/
u8 ufs_query_params[][5] = {
	/* The first index is not used for query operation */
	{0			,0				,0				,0	,0},

	{UFS_STD_WRITE_REQ	,UPIU_QUERY_OPCODE_SET_FLAG	,UPIU_FLAG_ID_DEVICEINIT	,0	,0},
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_FLAG	,UPIU_FLAG_ID_DEVICEINIT	,0	,0},

	/*
	 * INDEX of Configuration Descriptor means target LUN ranges to be configured,
	 * but this driver always set this to zero because it is supposed to configure less or equal than eight LUs.
	 */
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_DESC	,UPIU_DESC_ID_DEVICE		,0	,0},
	{UFS_STD_WRITE_REQ	,UPIU_QUERY_OPCODE_WRITE_DESC	,UPIU_DESC_ID_CONFIGURATION	,0	,0},
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_DESC	,UPIU_DESC_ID_CONFIGURATION	,0	,0},
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_DESC	,UPIU_DESC_ID_UNIT		,0	,0},
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_DESC	,UPIU_DESC_ID_UNIT		,UFS_RPMB_UNIT_DESCRIPTOR_INDEX	,0},
	/*
	 * INDEX of Unit Descriptor means target LUN, so this driver will override the value.
	 */
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_DESC	,UPIU_DESC_ID_GEOMETRY		,0	,0},
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_DESC	,UPIU_DESC_ID_STRING		,0	,0},

	{UFS_STD_WRITE_REQ	,UPIU_QUERY_OPCODE_WRITE_ATTR	,UPIU_ATTR_ID_BOOTLUNEN		,0	,0},
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_ATTR	,UPIU_ATTR_ID_BOOTLUNEN		,0	,0},
	{UFS_STD_WRITE_REQ	,UPIU_QUERY_OPCODE_WRITE_ATTR	,UPIU_ATTR_ID_REFCLKFREQ	,0	,0},
	{UFS_STD_READ_REQ	,UPIU_QUERY_OPCODE_READ_ATTR	,UPIU_ATTR_ID_REFCLKFREQ	,0	,0},
	{UFS_STD_READ_REQ,	UPIU_QUERY_OPCODE_READ_ATTR,	UPIU_ATTR_ID_DEVICEFFUSTATUS,	0,	0},
	{UFS_STD_WRITE_REQ, UPIU_QUERY_OPCODE_WRITE_ATTR,	UPIU_ATTR_ID_CONFIGDESCLOCK,	0,	0},
	{UFS_STD_READ_REQ,	UPIU_QUERY_OPCODE_READ_ATTR,		UPIU_ATTR_ID_CONFIGDESCLOCK,	0,	0},

	{},
};

/* UFS user command definition */
#if defined(WITH_LIB_CONSOLE)

#include <lib/console.h>

static int cmd_ufs(int argc, const console_cmd_args *argv)
{
    int rc = 0;

    if (argc < 2) {
notenoughargs:
        printf("not enough arguments:\n");
usage:
        return -1;
    }

    if (!strcmp(argv[1].str, "boot")) {
        if (argc < 3) goto notenoughargs;

	return ufs_bootlun_enable((int) argv[2].u);
    } else {
        printf("unrecognized subcommand\n");
        goto usage;
    }

    return rc;
}

STATIC_COMMAND_START
STATIC_COMMAND("ufs", "UFS commands", &cmd_ufs)
STATIC_COMMAND_END(ufs);

#endif

/* Containers that have a block device */
scsi_device_t *ufs_dev[SCSI_MAX_INITIATOR];
scsi_device_t ufs_dev_ssu;
scsi_device_t ufs_dev_rpmb;

static inline struct ufs_cal_param *ufs_get_cal_param(struct ufs_host *ufs)
{
#ifdef UFS_CAL_DIR
	return &ufs->cal_param;
#else
	return ufs->cal_param;
#endif
}

#ifdef UFS_CAL_DIR
/*
 * UFS CAL HELPER FOR Direct Method CAL
 *
 * This driver configures all the UIC by using these functions.
 * The functions exists in UFS CAL.
 */

static inline void ufs_u_delay(u32 us)
{
	u_delay(us);
}

static inline void ufs_map_vs_regions(struct ufs_host *ufs)
{
	ufs->handle.hci = (void *)ufs->ioaddr;
	ufs->handle.ufsp = (void *)ufs->vs_addr;
	ufs->handle.unipro = (void *)ufs->unipro_addr;
	ufs->handle.pma = (void *)ufs->phy_pma;
	ufs->handle.cport = (void *)ufs->vs_addr;	//TODO:
	ufs->handle.udelay = ufs_u_delay;
}

static int ufs_call_cal(struct ufs_host *ufs, int init, void *func)
{
	struct ufs_cal_param *p = &ufs->cal_param;
	int ret;
	cal_if_func_init fn_init;
	cal_if_func fn;

	if (init) {
		fn_init = (cal_if_func_init)func;
		ret = fn_init(p, ufs->host_index);
	} else {
		fn = (cal_if_func)func;
		ret = fn(p);
	}

	if (ret != UFS_CAL_NO_ERROR) {
		printf("%s: %d\n", __func__, ret);
		ret = -1;
	}

	return ret;
}

#else
/*
 * UFS CAL HELPER FOR OLD CAL
 *
 * This driver configures all the UIC by using these functions.
 * The functions exists in UFS CAL.
 */
static inline int ufs_init_cal(struct ufs_host *ufs, int idx)
{
	int ret = 0;

	ufs->cal_param->host = (void *)ufs;

	if (CONFIG_UFS_BOARD_TYPE)
		ufs->cal_param->board = BRD_UNIV;
	else
		ufs->cal_param->board = BRD_SMDK;


	ret = ufs_cal_init(ufs->cal_param, idx);
	if (ret != UFS_CAL_NO_ERROR) {
		printf("UFS Init failed with %d\n", ret);
		return ERR_GENERIC;
	}

	return NO_ERROR;
}

static inline int ufs_pre_link(struct ufs_host *ufs, u8 lane)
{
	int ret = 0;
	struct ufs_cal_param *p = ufs->cal_param;

	p->mclk_rate = ufs->mclk_rate;
	p->available_lane = lane;
	p->tbl = HOST_EMBD;

	ret = ufs_cal_pre_link(p);
	if (ret != UFS_CAL_NO_ERROR) {
		printf("ufs_pre_link failed with %d!!!\n", ret);
		return ERR_GENERIC;
	}

	return NO_ERROR;
}

static inline int ufs_post_link(struct ufs_host *ufs)
{
	int ret = 0;

	ret = ufs_cal_post_link(ufs->cal_param);
	if (ret != UFS_CAL_NO_ERROR) {
		printf("ufs_post_link failed with %d!!!\n", ret);
		return ERR_GENERIC;
	}

	return NO_ERROR;
}

static inline int ufs_pre_gear_change(struct ufs_host *ufs,
				struct uic_pwr_mode *pmd)
{
	struct ufs_cal_param *p = ufs->cal_param;
	int ret = 0;

	p->pmd = pmd;
	ret = ufs_cal_pre_pmc(p);
	if (ret != UFS_CAL_NO_ERROR) {
		printf("ufs_pre_gear_change failed with %d!!!\n", ret);
		return ERR_GENERIC;
	}

	return NO_ERROR;
}

static inline int ufs_post_gear_change(struct ufs_host *ufs)
{
	int ret = 0;

	ret = ufs_cal_post_pmc(ufs->cal_param);
	if (ret != UFS_CAL_NO_ERROR) {
		printf("ufs_post_gear_change failed with %d!!!\n", ret);
		return ERR_GENERIC;
	}

	return NO_ERROR;
}

/*
 * UFS CAL ADAPTOR
 *
 * This is called by UFS CAL in each project directory,
 * thus these are declared as global.
 */
void ufs_lld_dme_set(void *h, u32 addr, u32 val)
{
	struct ufs_host *ufs = (struct ufs_host *) h;
	struct ufs_uic_cmd cmd = { UIC_CMD_DME_SET, 0, 0, 0};

	cmd.uiccmdarg1 = addr;
	cmd.uiccmdarg3 = val;
	ufs->uic_cmd = &cmd;
	send_uic_cmd(ufs);
}

void ufs_lld_dme_get(void *h, u32 addr, u32 *val)
{
	struct ufs_host *ufs = (struct ufs_host *) h;
	struct ufs_uic_cmd cmd = { UIC_CMD_DME_GET, 0, 0, 0};

	cmd.uiccmdarg1 = addr;
	ufs->uic_cmd = &cmd;
	send_uic_cmd(ufs);
	*val = cmd.uiccmdarg3;
}

void ufs_lld_dme_peer_set(void *h, u32 addr, u32 val)
{
	struct ufs_host *ufs = (struct ufs_host *) h;
	struct ufs_uic_cmd cmd = { UIC_CMD_DME_PEER_SET, 0, 0, 0};

	cmd.uiccmdarg1 = addr;
	cmd.uiccmdarg3 = val;
	ufs->uic_cmd = &cmd;
	send_uic_cmd(ufs);
}

void ufs_lld_pma_write(void *h, u32 val, u32 addr)
{
	struct ufs_host *ufs = (struct ufs_host *) h;

	writel(val, VM_MEM(ufs->phy_pma + addr));
}

u32 ufs_lld_pma_read(void *h, u32 addr)
{
	struct ufs_host *ufs = (struct ufs_host *) h;

	return readl(VM_MEM(ufs->phy_pma + addr));
}

void ufs_lld_unipro_write(void *h, u32 val, u32 addr)
{
	struct ufs_host *ufs = (struct ufs_host *) h;

	writel(val, VM_MEM(ufs->unipro_addr + addr));
}

void ufs_lld_udelay(u32 val)
{
	u_delay(val);
}

void ufs_lld_usleep_delay(u32 min, u32 max)
{
	u_delay(max);
}

unsigned long ufs_lld_get_time_count(unsigned long offset)
{
	return offset;
}

unsigned long ufs_lld_calc_timeout(const unsigned int ms)
{
	return 1000 * ms;
}
#endif /* CAL HELPER END*/


/*
 * INTERNAL CORE FUNCTIONS
 */
struct ufs_host *get_cur_ufs_host(void)
{
	struct ufs_host *ufs = _ufs[_ufs_curr_host];
	if (!ufs)
		printf("Invalid ufs_host structure: %d !!!\n", _ufs_curr_host);
	return _ufs[_ufs_curr_host];
}

static void ufs_cache_flush(struct ufs_host *ufs, u32 type)
{
	clean_invalidate_dcache_range((u64)ufs->utrd_addr, (u64)(ufs->utrd_addr + 1));
	clean_invalidate_dcache_range((u64)ufs->cmd_desc_addr, (u64)(ufs->cmd_desc_addr + 1));
	if (type == UPIU_TRANSACTION_COMMAND && ufs->scsi_cmd->datalen)
		clean_invalidate_dcache_range((u64)ufs->scsi_cmd->buf, (u64)(ufs->scsi_cmd->buf + ufs->scsi_cmd->datalen));
}

static void __utp_map_sg(struct ufs_host *ufs)
{
	u32 i, len, sg_segments;

	len = ufs->scsi_cmd->datalen;

	if (len) {
		sg_segments = (len + UFS_SG_BLOCK_SIZE - 1) / UFS_SG_BLOCK_SIZE;
		for (i = 0; i < sg_segments; i++) {
			ufs->cmd_desc_addr->prd_table[i].size =
			    (u32) UFS_SG_BLOCK_SIZE - 1;
			ufs->cmd_desc_addr->prd_table[i].base_addr =
			    (u32)(((u64) PM_MEM(ufs->scsi_cmd->buf) + i * UFS_SG_BLOCK_SIZE) & (((u64)1 << UFS_BIT_LEN_OF_DWORD) - 1));
			ufs->cmd_desc_addr->prd_table[i].upper_addr =
			    (u32)(((u64) PM_MEM(ufs->scsi_cmd->buf) + i * UFS_SG_BLOCK_SIZE) >> UFS_BIT_LEN_OF_DWORD);
		}
	}
}

static u32 __utp_cmd_get_dir(scm *pscm)
{
	u32 data_direction;

	if (pscm->datalen) {
		switch (pscm->cdb[0]) {
		case SCSI_OP_UNMAP:
		case SCSI_OP_WRITE_10:
		case SCSI_OP_WRITE_BUFFER:
		case SCSI_OP_SECU_PROT_OUT:
			data_direction = UTP_HOST_TO_DEVICE;
			break;
		default:
			data_direction = UTP_DEVICE_TO_HOST;
			break;
		}
	} else
		data_direction = UTP_NO_DATA_TRANSFER;

	return data_direction;
}

static u32 __utp_cmd_get_flags(scm *pscm)
{
	u32 upiu_flags;

	if (pscm->datalen) {
		switch (pscm->cdb[0]) {
		case SCSI_OP_UNMAP:
		case SCSI_OP_WRITE_10:
		case SCSI_OP_WRITE_BUFFER:
		case SCSI_OP_SECU_PROT_OUT:
		case SCSI_MODE_SEL10:
			upiu_flags = UPIU_CMD_FLAGS_WRITE;
			break;
		default:
			upiu_flags = UPIU_CMD_FLAGS_READ;
			break;
		}
	} else
		upiu_flags = UPIU_CMD_FLAGS_NONE;

	return upiu_flags;
}

static void __utp_write_cmd_ucd(struct ufs_host *ufs)
{
	u32 datalen;

	struct ufs_upiu *cmd_ptr = &ufs->cmd_desc_addr->command_upiu;
	struct ufs_upiu_header *hdr = &cmd_ptr->header;
	u8 *tsf = cmd_ptr->tsf;

	u32 upiu_flags;

	upiu_flags = __utp_cmd_get_flags(ufs->scsi_cmd);

	/* header */
	hdr->type = UPIU_TRANSACTION_COMMAND;
	hdr->flags = upiu_flags;
	hdr->lun = ufs->lun;
	hdr->tag = 0;					/* Only tag #0 is used */

	/* Transaction Specific Fields */
	datalen = cpu_to_be32(ufs->scsi_cmd->datalen);
	memcpy(&tsf[0], &datalen, sizeof(u32));
	memcpy(&tsf[4], ufs->scsi_cmd->cdb, MAX_CDB_SIZE);
}

static void ufs_post_string_descriptor(struct ufs_host *ufs)
{
	struct ufs_string_desc *desc =
		(struct ufs_string_desc *)ufs->cmd_desc_addr->response_upiu.data;
	u16 *dst;
	size_t length;
	size_t bLength = (desc->bLength - 2) / 2;
	size_t i = 0;

	switch (ufs->string_query_type) {
		case STRING_MANUFACTURER_NAME:
			dst = ufs->manufacturer_name_string;
			length = MIN(bLength, sizeof(ufs->manufacturer_name_string) - 1);
			break;
		case STRING_PRODUCT_NAME:
			dst = ufs->product_name_string;
			length = MIN(bLength, sizeof(ufs->product_name_string) - 1);
			break;
		case STRING_OEM_ID:
			dst = ufs->oem_id_string;
			length = MIN(bLength, sizeof(ufs->oem_id_string) - 1);
			break;
		case STRING_SERIAL_NUMBER:
			dst = ufs->serial_number_string;
			length = MIN(bLength, sizeof(ufs->serial_number_string) - 1);
			break;
		case STRING_PRODUCT_REVISION:
			dst = ufs->product_revision_string;
			length = MIN(bLength, sizeof(ufs->product_revision_string) - 1);
			break;
		case STRING_UNSELECTED:
		default:
			return;
	}

	for ( ; i < length; i++)
		dst[i] = be16_to_cpu(desc->uc[i]);
	dst[i] = '\0';

	ufs->string_query_type = STRING_UNSELECTED;
}

static int __utp_write_query_ucd(struct ufs_host *ufs, query_index qry)
{
	int r = 0;
	int lun;

	struct ufs_upiu *cmd_ptr = &ufs->cmd_desc_addr->command_upiu;
	struct ufs_upiu_header *hdr = &cmd_ptr->header;
	u8 *tsf = cmd_ptr->tsf;
	u16 data_len;
	u16 data_len_be;
	u32 info;

	/*
	 * Data segment size and LENGTH
	 */
	if (ufs_query_params[qry][2] == UPIU_DESC_ID_CONFIGURATION)
		data_len = ufs->device_desc.bUD0BaseOffset +
			(8 * ufs->device_desc.bUDConfigPlength);
	else if (ufs_query_params[qry][1] == UPIU_QUERY_OPCODE_READ_DESC)
		data_len = UPIU_DATA_SIZE;
	else
		data_len = 0;

	/* header */
	hdr->type = UPIU_TRANSACTION_QUERY_REQ;
	hdr->flags = UPIU_CMD_FLAGS_NONE;
	hdr->tag = 0;					/* Only tag #0 is used */
	hdr->function = ufs_query_params[qry][0];	/* Query Function */
	data_len_be = cpu_to_be16(data_len);
	hdr->datalength = (ufs_query_params[qry][0] == UFS_STD_WRITE_REQ) ? data_len_be : 0;

	/* Transaction Specific Fields */
	tsf[0] = ufs_query_params[qry][1];		/* OPCODE */
	tsf[1] = ufs_query_params[qry][2];		/* IDN */
	tsf[2] = ufs_query_params[qry][3];		/* INDEX */
	tsf[3] = ufs_query_params[qry][4];		/* SELECTOR */

	memcpy(&tsf[6], &data_len_be, sizeof(u16));

	if (tsf[0] == UPIU_QUERY_OPCODE_WRITE_ATTR) {
		info = cpu_to_be32(ufs->attributes.arry[tsf[1]]);
		memcpy(&tsf[8], &info, sizeof(u32));
	} else if (tsf[0] == UPIU_QUERY_OPCODE_SET_FLAG)
		tsf[11] = (u8)ufs->flags.arry[tsf[1]];

	/* Data */
	if (tsf[0] == UPIU_QUERY_OPCODE_WRITE_DESC) {
		if (tsf[1] == UPIU_DESC_ID_CONFIGURATION) {

			/* setup config desc header */
			memcpy(cmd_ptr->data, &ufs->config_desc.header, ufs->device_desc.bUD0BaseOffset);

			/* setup config desc param of each LU */
			for (lun = 0; lun < 8; lun++)
				memcpy(cmd_ptr->data + ufs->device_desc.bUD0BaseOffset + lun*ufs->device_desc.bUDConfigPlength,
					&ufs->config_desc.unit[lun], ufs->device_desc.bUDConfigPlength);

			printf("UFS_QUERY_WRITE_DESC_CONFIG, len : %02x\n", data_len);
#ifdef SCSI_UFS_DEBUG
			u32 i, j, k;
			printf("==== ufs configuration descriptor setup ====\n");
			for (i = 0; i < ufs->device_desc.bUD0BaseOffset; i++)
				printf("  0x%02x", cmd_ptr->data[i]);
			printf("\n\n");
			j = i;
			for (k = 0; k < SCSI_MAX_DEVICE; k++) {
				for (i = 0; i < ufs->device_desc.bUDConfigPlength; i++)
					printf("  0x%02x", cmd_ptr->data[j + i + k * ufs->device_desc.bUDConfigPlength]);
				printf("\n");
			}
			printf("\n==========================================\n");
#endif
		} else
			memcpy(cmd_ptr->data, &ufs->config_desc, data_len);
	}

	return r;
}

static int __utp_write_utrd(struct ufs_host *ufs, u32 type)
{
	int r = 0;

	struct ufs_utrd *utrd_ptr = ufs->utrd_addr;
	u32 len;
	u16 sg_segments;

	u32 data_direction;

	/*
	 * You don't need to consider UPIU_TRANSACTION_NOP_OUT case,
	 * because NOP OUT packet data is all zeros.
	 */
	utrd_ptr->dw[2] = (u32)(OCS_INVALID_COMMAND_STATUS);
	switch (type) {
	case UPIU_TRANSACTION_COMMAND:
		len = ufs->scsi_cmd->datalen;
		sg_segments = (u16)((len + UFS_SG_BLOCK_SIZE - 1) / UFS_SG_BLOCK_SIZE);
		data_direction = __utp_cmd_get_dir(ufs->scsi_cmd);

		utrd_ptr->dw[0] = (u32)(data_direction | UTP_SCSI_COMMAND | UTP_REQ_DESC_INT_CMD);
		if (len) {
			if (ufs->quirks & UFSHCD_QUIRK_PRDT_BYTE_GRAN) {
				utrd_ptr->prdt_len = sg_segments * sizeof(struct ufs_prdt);
				utrd_ptr->prdt_off = (ALIGNED_UPIU_SIZE * 2);
			} else {
				utrd_ptr->prdt_len = sg_segments;
				utrd_ptr->prdt_off = (ALIGNED_UPIU_SIZE >> 1); /* 2048 B / 4 = 512 DWORDs */
			}
		} else {
			utrd_ptr->prdt_len = 0;
			utrd_ptr->prdt_off = 0;
		}

		break;
	case UPIU_TRANSACTION_QUERY_REQ:
		utrd_ptr->dw[0] = UTP_REQ_DESC_INT_CMD;
		utrd_ptr->prdt_len = 0;

		break;
	case UPIU_TRANSACTION_NOP_OUT:
		break;
	default:
		r = -1;
		printf("UFS: type %02x is not supported\n", type);
		break;
	}

	return r;
}

static int __utp_write_cmd_all_descs(struct ufs_host *ufs)
{
	/* ucd */
	__utp_write_cmd_ucd(ufs);

	/* prdt */
	__utp_map_sg(ufs);

	/* utrd*/
	return __utp_write_utrd(ufs, UPIU_TRANSACTION_COMMAND);
}

static int __utp_write_query_all_descs(struct ufs_host *ufs, query_index qry)
{
	/* ucd */
	__utp_write_query_ucd(ufs, qry);

	/* utrd*/
	return __utp_write_utrd(ufs, UPIU_TRANSACTION_QUERY_REQ);
}

/********************************************************************************
 * ufs minimal interrupt handler
 */
static int handle_ufs_uic_int(struct ufs_host *ufs, u32 intr_stat)
{
	int ret = UFS_IN_PROGRESS;
	struct ufs_uic_cmd *uic_cmd = ufs->uic_cmd;

	/*
	 * Check completion
	 *
	 * In some cases, you need to check more.
	 */
	if (intr_stat & UIC_COMMAND_COMPL) {
		/* Link startup */
		if (uic_cmd->uiccmdr == UIC_CMD_DME_LINK_STARTUP) {
			ret = UFS_NO_ERROR;
		/* Gear change */
		} else if (uic_cmd->uiccmdr == UIC_CMD_DME_SET &&
				ufs->uic_cmd->uiccmdarg1 == (0x1571 << 16)) {
			if (intr_stat & UIC_POWER_MODE)
				ret = UFS_NO_ERROR;
		} else {
			ret = UFS_NO_ERROR;
		}
	}

	/*
	 * Expected error case
	 *
	 * In case of link startup, bogus uic error is raised for
	 * LINE-RESET.
	 */
	if (intr_stat & UIC_ERROR &&
			uic_cmd->uiccmdr != UIC_CMD_DME_LINK_STARTUP) {
		printf("UFS: UIC ERROR 0x%08x\n", intr_stat);
		ret = UFS_ERROR;
	}

	return ret;
}

static int handle_ufs_utp_int(struct ufs_host *ufs, u32 intr_stat)
{
	int ret = UFS_IN_PROGRESS;

	if (intr_stat & UTP_TRANSFER_REQ_COMPL) {
		if (!(readl(VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_DOOR_BELL)) & 1))
			ret = UFS_NO_ERROR;
	}

	return ret;
}

int handle_ufs_int(struct ufs_host *ufs, int is_uic)
{
	u32 intr_stat;
	int ret;

	intr_stat = readl(VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS));

	if (is_uic)
		ret = handle_ufs_uic_int(ufs, intr_stat);
	else
		ret = handle_ufs_utp_int(ufs, intr_stat);

	/* Fatal error case */
	if (intr_stat & INT_FATAL_ERRORS) {
		printf("UFS: FATAL ERROR 0x%08x\n", intr_stat);
		ret = UFS_ERROR;
	}

	/* Terminate if success, error or progress, try again elsewhere */
	if (ret == UFS_IN_PROGRESS) {
		if (ufs->timeout--)
			u_delay(1);
		else {
			ret = UFS_TIMEOUT;
			if (is_uic) {
				printf("UFS UIC TIMEOUT\n");
				exynos_ufs_get_uic_info(ufs);
			} else
				printf("UFS UTP TIMEOUT\n");

			if ((intr_stat & UIC_ERROR) && !is_uic) {
				printf("UFS UIC error reported, UECPA: 0x%x UECDL : 0x%x\n",
					readl(ufs->ioaddr+REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER),
					readl(ufs->ioaddr+REG_UIC_ERROR_CODE_DATA_LINK_LAYER));
				if (boot_with_usb())
					ufs_sec_nad_uic_check();
			}
		}
	}

#ifdef SCSI_UFS_DEBUG
	if (ret != UFS_IN_PROGRESS)
		printf("UFS: INTERRUPT STATUS 0x%08x\n", intr_stat);
#endif

	return ret;
}

int send_uic_cmd(struct ufs_host *ufs)
{
	int err = 0, error_code;

	writel(ufs->uic_cmd->uiccmdarg1, VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_1));
	writel(ufs->uic_cmd->uiccmdarg2, VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_2));
	writel(ufs->uic_cmd->uiccmdarg3, VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_3));
	writel(ufs->uic_cmd->uiccmdr, VM_MEM(ufs->ioaddr + REG_UIC_COMMAND));

	ufs->timeout = UIC_CMD_TIMEOUT;	/* usec unit */
	while (UFS_IN_PROGRESS == (err = handle_ufs_int(ufs, 1)));
	writel(readl(VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS)),
			VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS));

	error_code = readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_2));
	if (err || error_code) {
		printf("UFS(%d) UIC command error!\n\t%08x %08x %08x %08x\n\t%08x %08x %08x %08x\n",
			ufs->host_index, ufs->uic_cmd->uiccmdr, ufs->uic_cmd->uiccmdarg1,
			ufs->uic_cmd->uiccmdarg2, ufs->uic_cmd->uiccmdarg3,
			readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND)),
			readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_1)),
			readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_2)),
			readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_3)));
			exynos_ufs_get_uic_info(ufs);
	}
	if (ufs->uic_cmd->uiccmdr == UIC_CMD_DME_GET
	    || ufs->uic_cmd->uiccmdr == UIC_CMD_DME_PEER_GET) {
		ufs->uic_cmd->uiccmdarg3 = readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_3));
#ifdef SCSI_UFS_DEBUG
		printf
		    ("UFS(%d) UIC_CMD_DME_(PEER)_GET command\n\t%08x %08x %08x %08x\n\t%08x %08x %08x %08x\n",
		     ufs->host_index, ufs->uic_cmd->uiccmdr, ufs->uic_cmd->uiccmdarg1,
		     ufs->uic_cmd->uiccmdarg2, ufs->uic_cmd->uiccmdarg3,
		     readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND)),
		     readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_1))
		     , readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_2)),
		     readl(VM_MEM(ufs->ioaddr + REG_UIC_COMMAND_ARG_3)));
#endif
	}

	return error_code | err;
}

static void __utp_send(struct ufs_host *ufs, u32 type)
{

	switch (type) {
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_QUERY_REQ:
		writel(0x0, VM_MEM(ufs->vs_addr + VS_UTRL_NEXUS_TYPE));
		break;
	case UPIU_TRANSACTION_COMMAND:
		writel(0xFFFFFFFF, VM_MEM(ufs->vs_addr + VS_UTRL_NEXUS_TYPE));
	default:
		break;
	}

	if (!ufs->iocc)
		ufs_cache_flush(ufs, type);

	switch (type & 0xff) {
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_COMMAND:
	case UPIU_TRANSACTION_QUERY_REQ:
		writel(1, VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_DOOR_BELL));
		break;
	default:
		break;
	}
}

static int __utp_wait_for_response(struct ufs_host *ufs, u32 type)
{
	int err = UFS_IN_PROGRESS;
	u32 timeout;

	/* set timeout */
	switch (type) {
	case UPIU_TRANSACTION_COMMAND:
		switch (ufs->scsi_cmd->cdb[0]) {
			case SCSI_OP_FORMAT_UNIT:
				timeout = FORMAT_CMD_TIMEOUT;
				break;
			case SCSI_OP_WRITE_BUFFER:
				/*
				 * According to the spec, software wait for a completion of ffu
				 * as long as the value of bFFUTimeout and the clock to make a time
				 * unit is accuate to work properly.
				 * We don't want to suffer from its malfunction and that's why
				 * we use the value that is considered much bigger.
				 * Even if it's too much bigger value, humans can recognize not
				 * responding symptom. Besides, ffu events happen extremely rarely.
				 */
				timeout = FFU_TIMEOUT;
				break;
			default:
				timeout = UTP_CMD_TIMEOUT;
				break;
		}
		break;
	case UPIU_TRANSACTION_NOP_OUT:
		timeout = NOP_OUT_TIMEOUT;
		break;
	case UPIU_TRANSACTION_QUERY_REQ:
		timeout = QUERY_REQ_TIMEOUT;
		break;
	default:
		printf("UFS: %s: invalid type = %u\n", __func__, type);
		goto end;
	}
	ufs->timeout = timeout;

	/* wait for completion */
	while (UFS_IN_PROGRESS == (err = handle_ufs_int(ufs, 0)))
		;
	writel(readl(VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS)),
			VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS));

	if (!ufs->iocc)
		ufs_cache_flush(ufs, type);

	/* Nexus configuration */
	if (type == UPIU_TRANSACTION_NOP_OUT || type == UPIU_TRANSACTION_QUERY_REQ)
		writel((readl(VM_MEM(ufs->vs_addr + VS_UTRL_NEXUS_TYPE)) | 0x01), VM_MEM(ufs->vs_addr + VS_UTRL_NEXUS_TYPE));
end:
	return err;
}

static void __utp_init(struct ufs_host *ufs, u32 lun)
{
	ufs->lun = lun;
	ufs->scsi_cmd = NULL;
	memset(ufs->cmd_desc_addr, 0x00, sizeof(struct ufs_cmd_desc));
}

static void __utp_query_read_info(struct ufs_host *ufs, u8 idn)
{
	struct ufs_upiu *resp_ptr = &ufs->cmd_desc_addr->response_upiu;
	u8 *data = resp_ptr->data;
	void *dst = NULL;
	size_t len = 0;
	int lun;

	len = data[0];
	switch (idn) {
	case UPIU_DESC_ID_UNIT:
		if (data[2] == UFS_RPMB_UNIT_DESCRIPTOR_INDEX)
			dst = &ufs->rpmb_unit_desc;
		else
			dst = &ufs->unit_desc[data[2]];
		break;
	case UPIU_DESC_ID_DEVICE:
		dst = &ufs->device_desc;
		break;
	case UPIU_DESC_ID_CONFIGURATION:
		dst = &ufs->config_desc;

		if (!ufs->device_desc.bUDConfigPlength || !ufs->device_desc.bUD0BaseOffset) {
			printf("UFS err during update congif_desc, device_desc.bUD0BaseOffset : %d, device_desc.bUDConfigPlength : %d\n",
				ufs->device_desc.bUD0BaseOffset, ufs->device_desc.bUDConfigPlength);
			return;
		}
		break;
	case UPIU_DESC_ID_GEOMETRY:
		dst = &ufs->geometry_desc;
		break;
	case UPIU_DESC_ID_STRING:
		ufs_post_string_descriptor(ufs);
		break;
	default:
		break;
	}

	if (dst) {
		if (idn == UPIU_DESC_ID_CONFIGURATION) {
			/* update config desc header */
			memcpy(&ufs->config_desc.header, resp_ptr->data, ufs->device_desc.bUD0BaseOffset);
			/* update config desc param of each LU */
			for (lun = 0; lun < 8; lun++)
				memcpy(&ufs->config_desc.unit[lun],
				resp_ptr->data + ufs->device_desc.bUD0BaseOffset
						+ (lun * ufs->device_desc.bUDConfigPlength),
				ufs->device_desc.bUDConfigPlength);

#ifdef SCSI_UFS_DEBUG
			u32 i;
			printf("==== ufs configuration descriptor response ====\n");
			for (i = 0; i < UPIU_DATA_SIZE/4; i++) {
				printf("  0x%08x", ___swab32(ufs->cmd_desc_addr->response_upiu.data[i]));
				if (((i + 1) % 8) == 0)
					printf("\n");
			}
			printf("\n==========================================\n");
#endif

		} else
			memcpy(dst, resp_ptr->data, len);
	}
}

static void __utp_query_get_data(struct ufs_host *ufs, query_index qry)
{
	struct ufs_upiu *resp_ptr = &ufs->cmd_desc_addr->response_upiu;
	u8 opcode = ufs_query_params[qry][1];

	u8 *tsf;
	u32 val;

	switch (opcode) {
	case UPIU_QUERY_OPCODE_READ_DESC:
		__utp_query_read_info(ufs, ufs_query_params[qry][2]);
		break;
	case UPIU_QUERY_OPCODE_READ_ATTR:
		tsf = resp_ptr->tsf;
		val = UPIU_HEADER_DWORD((u32) tsf[8], (u32) tsf[9], (u32) tsf[10], (u32) tsf[11]);
		ufs->attributes.arry[ufs_query_params[qry][2]] = val;
		break;
	case UPIU_QUERY_OPCODE_READ_FLAG:
		tsf = resp_ptr->tsf;
		val = (u32)tsf[11];
		ufs->flags.arry[ufs_query_params[qry][2]] = val;
		break;
	case UPIU_QUERY_OPCODE_SET_FLAG:
	case UPIU_QUERY_OPCODE_WRITE_DESC:
	case UPIU_QUERY_OPCODE_WRITE_ATTR:
		break;
	default:
		printf("UFS: %s: query with opcode 0x%02x is not supported\n", __func__, opcode);
		break;
	}
}

static int __utp_check_result(struct ufs_host *ufs)
{
	const char resp_msg[2][20] = { "Target Success", "Target Failure" };
	int r = 0;
	int ocs_broken = ufs->quirks & UFSHCD_QUIRK_BROKEN_OCS_FATAL_ERROR;
	struct ufs_utrd *utrd_ptr = ufs->utrd_addr;
	struct ufs_upiu *resp_ptr = &ufs->cmd_desc_addr->response_upiu;
	struct ufs_upiu_header *hdr = &resp_ptr->header;
	scm * pscm = ufs->scsi_cmd;

	/* Update SCSI status. SCSI would handle it.. */
	if (pscm)
		pscm->status = hdr->status;

	/* OCS - Overall Command Status */
	if (utrd_ptr->dw[2] != OCS_SUCCESS) {
		if ((ocs_broken && utrd_ptr->dw[2] != OCS_FATAL_ERROR) || !ocs_broken) {
		printf("Type (0x%x), OCS (0x%0x) \n", hdr->type, utrd_ptr->dw[2]);
		print_ufs_upiu(ufs, UFS_DEBUG_UPIU_ALL);
		r = -1;
		}
	}

	/* Response */
	if (hdr->type == UPIU_TRANSACTION_RESPONSE) {
		/* Copy sense data */
		memcpy(ufs->scsi_cmd->sense_buf, &resp_ptr->data[2], 18);

		if (!pscm) {
			printf("SCSI pscm is null\n");
			r = -1;
			goto error;
		}

		/* Check command set status values from response upiu */
		if (hdr->status) {
			printf("SCSI Status Value (%02x) \n", hdr->status);
			printf("SCSI cdb : %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
					pscm->cdb[0], pscm->cdb[1], pscm->cdb[2], pscm->cdb[3],
					pscm->cdb[4], pscm->cdb[5], pscm->cdb[6], pscm->cdb[7],
					pscm->cdb[8], pscm->cdb[9]);
			printf("SCSI Response(%02x) \n", hdr->response);
			printf("SCSI Sense - RESPONSE CODE (%02x), SENSE KEY (%02x), ASC/ASCQ (%02x/%02x) \n",
					pscm->sense_buf[0],
					pscm->sense_buf[2],
					pscm->sense_buf[12],
					pscm->sense_buf[13]);
			printf("SCSI Sense raw data : %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
					pscm->sense_buf[0], pscm->sense_buf[1], pscm->sense_buf[2], pscm->sense_buf[3], pscm->sense_buf[4],
					pscm->sense_buf[5], pscm->sense_buf[6], pscm->sense_buf[7], pscm->sense_buf[8], pscm->sense_buf[9],
					pscm->sense_buf[10], pscm->sense_buf[11], pscm->sense_buf[12], pscm->sense_buf[13], pscm->sense_buf[14],
					pscm->sense_buf[15], pscm->sense_buf[16], pscm->sense_buf[17]);
			r = -1;
		}
	} else if (hdr->type == UPIU_TRANSACTION_QUERY_RSP && hdr->response) {
		printf("UFS QUERY Response(%02x) : ", hdr->response);
		r = -1;
	}
error:
	if (hdr->response) {
		printf("%s for type 0x%02x\n", resp_msg[hdr->response], hdr->type);
		r = -1;
	}
	return r;
}

static void __utp_get_scsi_cxt(struct ufs_host *ufs, scm * pscm) {
	u32 lun;

	ufs->scsi_cmd = pscm;
	ufs->sense_buffer = pscm->sense_buf;
	ufs->sense_buflen = 64;	/* defined in include/scsi.h */

	if (pscm->sdev->lun == LUN_DEV_RPMB)
		lun = UFS_UPIU_RPMB_WLUN;
	else if (pscm->sdev->lun == LUN_DEV_SSU)
		lun = UFS_UPIU_UFS_DEVICE_WLUN;
	else
		lun = pscm->sdev->lun;

	ufs->lun = lun;
}

/* Check the sense data to clear unit attention status */
int ufs_check_sense(struct ufs_host *ufs)
{
	u8 *p = ufs->scsi_cmd->sense_buf;

	if (p[0] == 0x70)
		return (p[2] & 0xf);
	else
		return 0;
}

/*
 * This function does things on submitting COMMAND UPIU,
 * wait for and check RESPONSE UPIU. And then it reports
 * the result to the upper layer.
 */
int ufs_utp_cmd_process(struct ufs_host *ufs, scm * pscm)
{
	int r;
	int sense_ret = 0;
	int sense_err = 0;
	u32 type = UPIU_TRANSACTION_COMMAND;
	int retry = 5;

	do {
uac_retry:
		/* Init context */
		__utp_init(ufs, 0);

		/* Get context from SCSI */
		__utp_get_scsi_cxt(ufs, pscm);

		/* Describe all descriptors */
		r = __utp_write_cmd_all_descs(ufs);
		if (r != 0) {
			ufs_err("UFS utp write desc error(%d): 0x%x\n", r, type);
			goto end;
		}

		wmb();
		/* Submit a command */
		__utp_send(ufs, type);

		/* Wait for response */
		r = __utp_wait_for_response(ufs, type);
		if (r != 0) {
			ufs_err("UFS utp interface error(%d): 0x%x\n", r, type);
			goto end;
		}

		/* Get and check result */
		sense_ret = __utp_check_result(ufs);
		sense_err = ufs_check_sense(ufs);

		if (sense_ret != 0) {
			printf("UFS utp check result error: %d\n", sense_ret);
			if (sense_err == SENSE_UNIT_ATTENTION) {
				printf("UFS unit attention: %d, retry : %d\n", sense_ret, retry);
				retry--;
				if (retry <= 0)
					break;
				goto uac_retry; // if uac no need to recovery
			} else if (sense_err == SENSE_MEDIUM_ERROR) {
				printf("UFS Medium err: 0x%x\n", sense_ret);
				break;
			} else if (sense_err == SENSE_HARDWARE_ERROR) {
				printf("UFS hardware err: 0x%x\n", sense_ret);
				break;
			}
		}
end:
		if ((r == UFS_ERROR) || (r == UFS_TIMEOUT)) {
			printf("UFS recovery started. retry : %d cmd: 0x%x\n", retry, pscm->cdb[0]);
			ufs_init_interface(ufs);
		}
	} while (retry-- && r);

	/*
	 * 1. EDL & Factory binary
	 *	1) SCSI cmd timeout: hang
	 *	2) Medium error & Handware error: check scsi cmd and hang
	 * 2. Normal booting & Factory binary
	 *	1) Medium error & Handware error: upload
	 */
	if (r && boot_with_usb())
		ufs_sec_nad_cmd_check(ufs, pscm, r);

#if defined(CONFIG_SEC_FACTORY_BUILD)
	if ((sense_err == SENSE_MEDIUM_ERROR)
		|| (sense_err == SENSE_HARDWARE_ERROR))
		ufs_sec_device_err_upload(sense_err);
#endif

#ifdef SCSI_UFS_DEBUG
	print_ufs_upiu(ufs, UFS_DEBUG_UPIU);
#endif

	ptempscm = pscm;

	if (sense_ret)
		return sense_ret;
	else
		return r;
}

/*
 * This function does things on submitting NOP OUT UPIU,
 * wait for and check NOP IN UPIU.
 */
static int ufs_utp_nop_process(struct ufs_host *ufs)
{
	int r;
	u32 type = UPIU_TRANSACTION_NOP_OUT;

	/*
	 * Init context. NOP OUT should be filled with zero
	 * except for Task Tag, but we only use tag #0.
	 * Therefore, therer is not necessary to write descriptors in here.
	 */
	__utp_init(ufs, 0);

	/* Describe UTRD */
	r = __utp_write_utrd(ufs, type);
	if (r != 0) {
		ufs_err("UFS utp write desc error(%d): 0x%x\n", r, type);
		goto end;
	}

	wmb();
	/* Submit a command */
	__utp_send(ufs, type);

	/* Wait for response */
	r = __utp_wait_for_response(ufs, type);
	if (r != 0) {
		ufs_err("UFS utp interface error(%d): 0x%x\n", r, type);
		goto end;
	}

	/* Get and check result */
	r = __utp_check_result(ufs);
	if (r != 0) {
		ufs_err("UFS utp check result error(%d): 0x%x\n", r, type);
		goto end;
	}
end:
#ifdef SCSI_UFS_DEBUG
	print_ufs_upiu(ufs, UFS_DEBUG_UPIU);
#endif

	return r;
}

/*
 * This function does things on submitting QUERY REQUEST UPIU,
 * wait for and check QUERY RESPONSE UPIU.
 */
int ufs_utp_query_process(struct ufs_host *ufs, query_index qry, u32 lun)
{
	struct ufs_upiu *cmd_ptr = &ufs->cmd_desc_addr->command_upiu;
	struct ufs_upiu_header *hdr = &cmd_ptr->header;

	int r;
	u32 type = UPIU_TRANSACTION_QUERY_REQ;

	/* Init context */
	__utp_init(ufs, lun);

	/* Describe all descriptors */
	r = __utp_write_query_all_descs(ufs, qry);
	if (r != 0) {
		ufs_err("UFS utp write desc error(%d): 0x%x\n", r, type);
		goto end;
	}

	wmb();
	/* Submit a command */
	__utp_send(ufs, type);

	/* Wait for response */
	r = __utp_wait_for_response(ufs, type);
	if (r != 0) {
		ufs_err("UFS utp interface error(%d): 0x%x\n", r, type);
		goto end;
	}

	/* Get and check result */
	r = __utp_check_result(ufs);
	if (r != 0) {
		ufs_err("UFS utp check result error(%d): 0x%x\n", r, type);
		goto end;
	}

	__utp_query_get_data(ufs, qry);

end:
	if (r != 0)
		printf("UFS Query cmd: 0x%x, flag 0x%x, function 0x%x, response 0x%x\n",
			qry, hdr->flags, hdr->function, hdr->response);

#ifdef SCSI_UFS_DEBUG
	print_ufs_upiu(ufs, UFS_DEBUG_UPIU);
#endif

	return r;
}

int ufs_utp_query_retry(struct ufs_host *ufs, query_index qry, u32 lun)
{
	int r = 0;
	int retries;

	for (retries = QUERY_REQ_RETRIES; retries > 0; retries --) {
		r = ufs_utp_query_process(ufs, qry, lun);

		if (r)
			printf("UFS Query retries error: %d, reties: %d\n", r, retries);
		else
			break;
	}

	if (r && boot_with_usb())
		ufs_sec_nad_err_logging(qry, UFS_NAD_QUERY_ERR);

	return r;
}

static const u16 *ufs_get_string(enum string_query_type_t type)
{
	struct ufs_host *ufs = get_cur_ufs_host();
	if (!ufs->device_desc.bLength) {
		printf("device descriptor must be read first\n");
		return NULL;
	}

	static const u16 string_not_available[] = {'N', '/', 'A', '\0'};
	const u16 *string = string_not_available;

	switch (type) {
		case STRING_MANUFACTURER_NAME:
			if (!(*ufs->manufacturer_name_string)) {
				ufs_query_params[DESC_R_STRING_DESC][3] = ufs->device_desc.iManufacturerName;
				ufs->string_query_type = type;
				ufs_utp_query_retry(ufs, DESC_R_STRING_DESC, 0);
			}
			string = ufs->manufacturer_name_string;
			break;
		case STRING_PRODUCT_NAME:
			if (!(*ufs->product_name_string)) {
				ufs_query_params[DESC_R_STRING_DESC][3] = ufs->device_desc.iProductName;
				ufs->string_query_type = type;
				ufs_utp_query_retry(ufs, DESC_R_STRING_DESC, 0);
			}
			string = ufs->product_name_string;
			break;
		case STRING_OEM_ID:
			if (!(*ufs->oem_id_string)) {
				ufs_query_params[DESC_R_STRING_DESC][3] = ufs->device_desc.iOemID;
				ufs->string_query_type = type;
				ufs_utp_query_retry(ufs, DESC_R_STRING_DESC, 0);
			}
			string = ufs->oem_id_string;
			break;
		case STRING_SERIAL_NUMBER:
			if (!(*ufs->serial_number_string)) {
				ufs_query_params[DESC_R_STRING_DESC][3] = ufs->device_desc.iSerialNumber;
				ufs->string_query_type = type;
				ufs_utp_query_retry(ufs, DESC_R_STRING_DESC, 0);
			}
			string = ufs->serial_number_string;
			break;
		case STRING_PRODUCT_REVISION:
			if ((be16_to_cpu(ufs->device_desc.wSpecVersion) >> 8) < 3) {
				printf("UFS %u.%u doesn't support Product Revision String\n",
						be16_to_cpu(ufs->device_desc.wSpecVersion) >> 8,
						be16_to_cpu(ufs->device_desc.wSpecVersion) & 0xFF);
				break;
			}
			if (!(*ufs->product_revision_string)) {
				ufs_query_params[DESC_R_STRING_DESC][3] = ufs->device_desc.iProductRevisionLevel;
				ufs->string_query_type = type;
				ufs_utp_query_retry(ufs, DESC_R_STRING_DESC, 0);
			}
			string = ufs->product_revision_string;
			break;
		case STRING_UNSELECTED:
		default:
			break;
	}

	return string;
}

/*
 * CALLBACK FUNCTION: scsi_exec
 *
 * This is called for SCSI stack to process some SCSI commands.
 * This is registered in SCSI stack when executing scsi_scan().
 */
static status_t scsi_exec(scm * pscm)
{
	struct ufs_host *ufs;

	if (!pscm)
		return ERR_NOT_VALID;

	ptempscm = pscm;

	ufs = get_cur_ufs_host();

	return ufs_utp_cmd_process(ufs, pscm);
}

scsi_device_t *scsi_get_ssu_sdev(void)
{
	return (struct scsi_device_s *)&ufs_dev_ssu;
}

int ufs_bootlun_enable(int enable)
{
	struct ufs_host *ufs = get_cur_ufs_host();
	query_index qry = ATTR_W_BOOTLUNEN;

	ufs->attributes.arry[ufs_query_params[qry][2]] = enable;
	return ufs_utp_query_retry(ufs, qry, 0);
}

int ufs_get_bootlun(void)
{
	struct ufs_host *ufs = get_cur_ufs_host();

	ufs_utp_query_retry(ufs, ATTR_R_BOOTLUNEN, 0);
	return ufs->attributes.arry[UPIU_ATTR_ID_BOOTLUNEN];
}
static int ufs_mphy_unipro_setting(struct ufs_host *ufs, struct ufs_uic_cmd *uic_cmd_list)
{
	int res = 0;
	u32 timeout;

	if (!uic_cmd_list) {
		dprintf(INFO, "%s: cmd list is empty\n", __func__);
		return res;
	}

	while (uic_cmd_list->uiccmdr) {
		ufs->uic_cmd = uic_cmd_list++;
		switch (ufs->uic_cmd->uiccmdr) {
		case UIC_CMD_WAIT:
			u_delay(ufs->uic_cmd->uiccmdarg2);
			break;
		case UIC_CMD_WAIT_ISR:
			timeout = UIC_CMD_TIMEOUT;
			while ((readl(VM_MEM(ufs->ioaddr + ufs->uic_cmd->uiccmdarg1)) &
				ufs->uic_cmd->uiccmdarg2) != ufs->uic_cmd->uiccmdarg2) {
				if (!timeout--) {
					res = 0;
					goto out;
				}
				u_delay(1);
			}
			writel(ufs->uic_cmd->uiccmdarg2, VM_MEM(ufs->ioaddr + ufs->uic_cmd->uiccmdarg1));
			break;
		case UIC_CMD_REGISTER_SET:
			writel(ufs->uic_cmd->uiccmdarg2, VM_MEM(ufs->unipro_addr + ufs->uic_cmd->uiccmdarg1));
			break;
		case PHY_PMA_COMN_SET:
		case PHY_PMA_TRSV_SET:
			writel(ufs->uic_cmd->uiccmdarg2, VM_MEM(ufs->phy_pma + ufs->uic_cmd->uiccmdarg1));
			break;
		case PHY_PMA_COMN_WAIT:
		case PHY_PMA_TRSV_WAIT:
			timeout = UIC_CMD_TIMEOUT;
			while ((readl(VM_MEM(ufs->phy_pma + ufs->uic_cmd->uiccmdarg1)) &
				ufs->uic_cmd->uiccmdarg2) != ufs->uic_cmd->uiccmdarg2) {
				if (!timeout--) {
					res = 0;
					goto out;
				}
				u_delay(1);
			}
			break;
		default:
			res = send_uic_cmd(ufs);
			break;
		}
	}

out:
	if (res)
		dprintf(INFO, "%s: failed cmd:0x%x arg1:0x%x arg2:0x%x with err %d\n",
			__func__, ufs->uic_cmd->uiccmdr,
			ufs->uic_cmd->uiccmdarg1,
			ufs->uic_cmd->uiccmdarg2, res);
	return res;
}

static int ufs_update_max_gear(struct ufs_host *ufs)
{
	struct ufs_uic_cmd rx_cmd = { UIC_CMD_DME_GET, (0x1587 << 16), 0, 0 };	/* PA_MAXRXHSGEAR */
	struct ufs_cal_param *p;
	int ret = 0;
	u32 max_rx_hs_gear = 0;

	if (!ufs)
		return ret;

	p = ufs_get_cal_param(ufs);

	ufs->uic_cmd = &rx_cmd;
	ret = send_uic_cmd(ufs);
	if (ret)
		goto out;

	max_rx_hs_gear = ufs->uic_cmd->uiccmdarg3;
	p->max_gear = MIN(max_rx_hs_gear, ufs->gear);

	printf("ufs max_gear(%d)\n", p->max_gear);

out:
	return ret;
}

static int ufs_update_connected_lane(struct ufs_host *ufs)
{
	struct ufs_cal_param *p;
	int res = 0;
	int tx, rx;
	struct ufs_uic_cmd tx_cmd = { UIC_CMD_DME_GET, (0x1561 << 16), 0, 0 };	/* PA_ConnectedTxDataLane */
	struct ufs_uic_cmd rx_cmd = { UIC_CMD_DME_GET, (0x1581 << 16), 0, 0 };	/* PA_ConnectedRxDataLane */

	p = ufs_get_cal_param(ufs);

	ufs->uic_cmd = &tx_cmd;
	res = send_uic_cmd(ufs);
	if (res)
		goto out;
	tx = ufs->uic_cmd->uiccmdarg3;

	ufs->uic_cmd = &rx_cmd;
	res = send_uic_cmd(ufs);
	if (res)
		goto out;
	rx = ufs->uic_cmd->uiccmdarg3;

	p->connected_tx_lane = tx;
	p->connected_rx_lane = rx;

out:
	return res;
}

static int ufs_update_active_lane(struct ufs_host *ufs)
{
	int res = 0;
	struct ufs_uic_cmd tx_cmd = { UIC_CMD_DME_GET, (0x1560 << 16), 0, 0 };	/* PA_ACTIVETXDATALANES */
	struct ufs_uic_cmd rx_cmd = { UIC_CMD_DME_GET, (0x1580 << 16), 0, 0 };	/* PA_ACTIVERXDATALANES */
	struct ufs_cal_param *p;

	if (!ufs)
		return res;

	p = ufs_get_cal_param(ufs);

	ufs->uic_cmd = &tx_cmd;
	res = send_uic_cmd(ufs);
	if (res)
		goto out;
	p->active_tx_lane = ufs->uic_cmd->uiccmdarg3;

	ufs->uic_cmd = &rx_cmd;
	res = send_uic_cmd(ufs);
	if (res)
		goto out;

	p->active_rx_lane = ufs->uic_cmd->uiccmdarg3;
	printf("UFS active_tx_lane(%d), active_rx_lane(%d)\n", p->active_tx_lane, p->active_rx_lane);

	/* we just guarantee symmetric lanes */
	if (p->active_tx_lane != p->active_rx_lane) {
		printf("UFS assymmetric lanes\n");
		res = -1;
	}
out:
	return res;
}

static int ufs_check_2lane(struct ufs_host *ufs)
{
	int res = 0;
	int tx, rx;
	struct ufs_uic_cmd tx_cmd = { UIC_CMD_DME_GET, (0x1561 << 16), 0, 0 };	/* PA_ConnectedTxDataLane */
	struct ufs_uic_cmd rx_cmd = { UIC_CMD_DME_GET, (0x1581 << 16), 0, 0 };	/* PA_ConnectedRxDataLane */
	struct ufs_uic_cmd ufs_2lane_cmd[] = {
		{UIC_CMD_DME_SET, (0x1560 << 16), 0, 2}	/* PA_ActiveTxDataLanes */
		, {UIC_CMD_DME_SET, (0x1580 << 16), 0, 2}	/* PA_ActiveRxDataLanes */
		/* NULL TERMINATION */
		, {0, 0, 0, 0}
	};
	struct ufs_uic_cmd ufs_1lane_cmd[] = {
		 {UIC_CMD_DME_SET, (0x1560 << 16), 0, 1}	/* PA_ActiveTxDataLanes */
		, {UIC_CMD_DME_SET, (0x1580 << 16), 0, 1}	/* PA_ActiveRxDataLanes */
		/* NULL TERMINATION */
		, {0, 0, 0, 0}
	};

	if (!ufs)
		return res;

	ufs->uic_cmd = &tx_cmd;
	res = send_uic_cmd(ufs);
	if (res)
		goto out;
	tx = ufs->uic_cmd->uiccmdarg3;

	ufs->uic_cmd = &rx_cmd;
	res = send_uic_cmd(ufs);
	if (res)
		goto out;
	rx = ufs->uic_cmd->uiccmdarg3;

	/* In asymmetric cases, we use just one lane */
	if (tx != rx)
		rx = tx = 1;
	else if (tx != ufs->lane)
		rx = tx = MIN(ufs->lane, tx);

	/* Compare to set use host of lane */
	dprintf(INFO, "UFS host set Lanes rx: %d tx: %d\n", rx, tx);

	if (tx == 2 && rx == 2) {
		res = ufs_mphy_unipro_setting(ufs, ufs_2lane_cmd);
		if (res) {
			dprintf(INFO, "trying to use 2 lane connection, but fail...\n");
			goto out;
		}
		dprintf(INFO, "Use 2 lane connection\n");
	} else if (tx == 1 && rx == 1) {
		res = ufs_mphy_unipro_setting(ufs, ufs_1lane_cmd);
		if (res) {
			dprintf(INFO, "trying to use 1 lane connection, but fail...\n");
			goto out;
		}
		dprintf(INFO, "Use 1 lane connection\n");
	} else
		dprintf(INFO, "trying to use the abnormal lane connection tx=%d rx=%d, so it has failed...\n", tx, rx);

	/*
	 * I don't think it's necessary to check a number of Tx against
	 * a number of Rx.
	 */
	ufs->pmd_cxt.lane = tx;
 out:
	return res;
}


static int ufs_end_boot_mode(struct ufs_host *ufs)
{
	int flag = 1;
	int retry = FDEVICEINIT_TIMEOUT;
	int res;
	int i;
	uint64_t fdevice_start = current_time_hires();

	for (i = 0; i < NOP_OUT_RETRY; i++) {
		res = ufs_utp_nop_process(ufs);
		if (res == UFS_NO_ERROR)
			break;
	}

	if (res != UFS_NO_ERROR) {
		printf("UFS: NOP OUT failed\n");
		if (boot_with_usb())
			ufs_sec_nad_err_logging(UFS_ERR_NOP_TIMEOUT, UFS_NAD_INIT_ERR);
		goto end;
	}


	ufs->flags.arry[UPIU_FLAG_ID_DEVICEINIT] = flag;
	res = ufs_utp_query_retry(ufs, FLAG_W_FDEVICEINIT , 0);
	if (res) {
		printf("UFS: setting fDeviceInit flag failed with error: %d\n", res);
		goto end;
	}

	/*
	 * There are some retries inside handle_ufs_int, so the actual
	 * timeout for fDeviceInit must be more than 1.5secs.
	 * FDEVICEINIT_TIMEOUT is just to guarantee more than 1.5secs.
	 */
	do {
		res = ufs_utp_query_retry(ufs, FLAG_R_FDEVICEINIT, 0);
		if (res) {
			printf("UFS: reading fDeviceInit flag failed with error: %d\n", res);
			goto end;
		}

		flag = ufs->flags.arry[UPIU_FLAG_ID_DEVICEINIT];
		retry--;
		m_delay(1);
	} while (flag && retry > 0);

	if (flag) {
		printf("UFS: fdeviceinit fails: %d, %d, %llu\n", flag, res,
				current_time_hires() - fdevice_start);
		if (boot_with_usb())
			ufs_sec_nad_err_logging(UFS_ERR_FDEVICE_INIT_NOT_CLR, UFS_NAD_INIT_ERR);
		res = -1;
	}
end:
	return res;
}

int ufs_device_reset(struct ufs_host *ufs)
{
	writel(0, VM_MEM(ufs->vs_addr + VS_GPIO_OUT));
	u_delay(5);
	writel(1, VM_MEM(ufs->vs_addr + VS_GPIO_OUT));

	return 0;
}

static void ufs_device_power(struct ufs_host *ufs)
{

//	ufs_ctrl_dev_pwr(0);
	/*
	 * We need around 13msec delay to discharge completely
	 * and add some time margin for impedance values of
	 * various projects.
	 */
//	u_delay(2000);
	ufs_ctrl_dev_pwr(1);
}

static int ufs_pre_setup(struct ufs_host *ufs)
{
	u32 reg;

	/* VS_SW_RST */
	if ((readl(VM_MEM(ufs->vs_addr + VS_FORCE_HCS)) >> 4) & 0xf)
		writel(0x0, VM_MEM(ufs->vs_addr +  VS_FORCE_HCS));

	writel(3, VM_MEM(ufs->vs_addr + VS_SW_RST));
	while (readl(VM_MEM(ufs->vs_addr + VS_SW_RST)))
		;

	/* VENDOR_SPECIFIC_IS[20] : clear UFS_IDLE_Indicator bit (if UFS_LINK is reset, this bit is asserted) */
	reg = readl(VM_MEM(ufs->vs_addr + VS_IS));
	if ((reg >> 20) & 0x1)
		writel(reg, VM_MEM(ufs->vs_addr + VS_IS));


	ufs_device_power(ufs);
	u_delay(1000);
	ufs_device_reset(ufs);

	writel(1, VM_MEM(ufs->ioaddr + REG_CONTROLLER_ENABLE));

	while (!(readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_ENABLE)) & 0x1))
		u_delay(1);

	/*ctrl refclkout*/
	writel((readl(VM_MEM(ufs->vs_addr + VS_CLKSTOP_CTRL)) & ~(1 << 4)), VM_MEM(ufs->vs_addr + VS_CLKSTOP_CTRL));

	/*CLOCK GATING SET*/
	writel(0xde0, VM_MEM(ufs->vs_addr + VS_FORCE_HCS));

	writel(readl(VM_MEM(ufs->vs_addr + VS_UFS_ACG_DISABLE))|1, VM_MEM(ufs->vs_addr + VS_UFS_ACG_DISABLE));
	memset(ufs->cmd_desc_addr, 0x00, UFS_NUTRS*sizeof(struct ufs_cmd_desc));
	memset(ufs->utrd_addr, 0x00, UFS_NUTRS*sizeof(struct ufs_utrd));
	memset(ufs->utmrd_addr, 0x00, UFS_NUTMRS*sizeof(struct ufs_utmrd));
	ufs->utrd_addr->cmd_desc_addr_l = (u64)(ufs->cmd_desc_addr);

	if (ufs->quirks & UFSHCD_QUIRK_PRDT_BYTE_GRAN) {
		ufs->utrd_addr->rsp_upiu_off = (u16)(offsetof(struct ufs_cmd_desc, response_upiu));
		ufs->utrd_addr->rsp_upiu_len = (u16)(ALIGNED_UPIU_SIZE);
	} else {
		ufs->utrd_addr->rsp_upiu_off = (u16)(offsetof(struct ufs_cmd_desc, response_upiu)) >> 2;
		ufs->utrd_addr->rsp_upiu_len = (u16)(ALIGNED_UPIU_SIZE) >> 2;
	}

	writel((u64)ufs->utmrd_addr, VM_MEM(ufs->ioaddr + REG_UTP_TASK_REQ_LIST_BASE_L));
	writel(0, VM_MEM(ufs->ioaddr + REG_UTP_TASK_REQ_LIST_BASE_H));

	writel((u64)ufs->utrd_addr, VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_LIST_BASE_L));
	writel(0, VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_LIST_BASE_H));

	/* enable cport */
	writel(0x22, VM_MEM(ufs->vs_addr + 0x114));
	writel(1, VM_MEM(ufs->vs_addr + 0x110));

	return NO_ERROR;
}

static void ufs_vendor_setup(struct ufs_host *ufs)
{
	/* DMA little endian, order change */
	writel(0xa, VM_MEM(ufs->vs_addr + VS_DATA_REORDER));

	writel(1, VM_MEM(ufs->ioaddr + REG_UTP_TASK_REQ_LIST_RUN_STOP));

	writel(1, VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_LIST_RUN_STOP));

	writel(UFS_SG_BLOCK_SIZE_BIT, VM_MEM(ufs->vs_addr + VS_TXPRDT_ENTRY_SIZE));
	writel(UFS_SG_BLOCK_SIZE_BIT, VM_MEM(ufs->vs_addr + VS_RXPRDT_ENTRY_SIZE));

	writel(0xFFFFFFFF, VM_MEM(ufs->vs_addr + VS_UTRL_NEXUS_TYPE));
	writel(0xFFFFFFFF, VM_MEM(ufs->vs_addr + VS_UMTRL_NEXUS_TYPE));
}

static int ufs_pmc_common(struct ufs_host *ufs, struct uic_pwr_mode *pmd)
{
	u32 reg;
	int res = NO_ERROR;
	struct ufs_uic_cmd cmd[] = {
		{UIC_CMD_DME_SET, (0x1583 << 16), 0, ufs->gear}, /* PA_RxGear */
		{UIC_CMD_DME_SET, (0x1568 << 16), 0, ufs->gear}, /* PA_TxGear */
		{UIC_CMD_DME_SET, (0x1580 << 16), 0, 0}, /* PA_ActiveRxDataLanes */
		{UIC_CMD_DME_SET, (0x1560 << 16), 0, 0}, /* PA_ActiveTxDataLanes */
		{UIC_CMD_DME_SET, (0x1584 << 16), 0, 1}, /* PA_RxTermination */
		{UIC_CMD_DME_SET, (0x1569 << 16), 0, 1}, /* PA_TxTermination */
		{UIC_CMD_DME_SET, (0x156a << 16), 0, UFS_RATE}, /* PA_HSSeries */
		{0, 0, 0, 0}
	};

	struct ufs_uic_cmd pmc_cmd = {
		UIC_CMD_DME_SET, (0x1571 << 16), 0, UFS_RXTX_POWER_MODE
	};

	/* Modity a value to be set PA_ActiveXxDataLanes */
	cmd[2].uiccmdarg3 = pmd->lane;
	cmd[3].uiccmdarg3 = pmd->lane;

	res = ufs_mphy_unipro_setting(ufs, cmd);
	if (res)
		goto out;

	/* Actual power mode change */
	ufs->uic_cmd = &pmc_cmd;
	res = send_uic_cmd(ufs);
	if (res)
		goto out;
	//printf("Use 2 lane connection\n");

	reg = readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_STATUS));

	while (UPMCRS(reg) != PWR_LOCAL) {
		printf("UFS: gear change failed, UPMCRS = %x !!\n",
							UPMCRS(reg));
		res = ERR_GENERIC;
		reg = readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_STATUS));
		m_delay(1);
	}
out:
	return res;
}

/*
 * In this function, read device's bRefClkFreq attribute
 * and if attr is not 1h, change it to 1h which means 26MHz.
 * It's because Exynos always use 26MHz reference clock
 * and device should know about soc's ref clk value.
 */
static int ufs_ref_clk_setup(struct ufs_host *ufs)
{
	int res;

	res = ufs_utp_query_retry(ufs, ATTR_R_REFCLKFREQ, 0);
	if (res) {
		printf("UFS read ref clk failed\n");
		return res;
	} else {
		printf("UFS ref clk setting is %x\n", ufs->attributes.arry[UPIU_ATTR_ID_REFCLKFREQ]);
	}

	if (ufs->attributes.arry[UPIU_ATTR_ID_REFCLKFREQ] != ufs->refclk_rate) {
		ufs->attributes.arry[UPIU_ATTR_ID_REFCLKFREQ] = ufs->refclk_rate;
		res = ufs_utp_query_retry(ufs, ATTR_W_REFCLKFREQ, 0);
	}

	return res;
}

int ufs_init_interface(struct ufs_host *ufs)
{
	struct ufs_uic_cmd uic_cmd = { UIC_CMD_DME_LINK_STARTUP, 0, 0, 0};
	struct ufs_uic_cmd get_a_lane_cmd = { UIC_CMD_DME_GET, (0x1540 << 16), 0, 0 };
	struct uic_pwr_mode *pmd = &ufs->pmd_cxt;
	int res = -1;

	if (!ufs)
		return res;

	if (ufs_pre_setup(ufs)) {
		printf("UFS %d ufs_pre_setup error!\n", ufs->host_index);
		goto out;
	}

	/* 1. pre link */
	ufs->uic_cmd = &get_a_lane_cmd;
	if (send_uic_cmd(ufs)) {
		printf("UFS%d getting a number of lanes error!\n", ufs->host_index);
		goto out;
	}

#ifdef UFS_CAL_DIR
	struct ufs_cal_param *p;

	p = ufs_get_cal_param(ufs);
	p->mclk_rate = ufs->mclk_rate;
	p->available_lane = ufs->uic_cmd->uiccmdarg3;
	if (ufs_call_cal(ufs, 0, ufs_cal_pre_link))
		goto out;
#else
	if (ufs_pre_link(ufs, ufs->uic_cmd->uiccmdarg3))
		goto out;
#endif

	/* 2. link startup */
	ufs->uic_cmd = &uic_cmd;
	if (send_uic_cmd(ufs)) {
		printf("UFS%d linkstartup error!\n", ufs->host_index);
		if (boot_with_usb())
			ufs_sec_nad_err_logging(UFS_ERR_LINK_STARTUP, UFS_NAD_INIT_ERR);
		goto out;
	}

	/* 3. update max gear */
	if (ufs_update_max_gear(ufs))
		goto out;

	/* 3.1 update connected lane */
	if (ufs_update_connected_lane(ufs))
		goto out;

	/* 4. post link */
#ifdef UFS_CAL_DIR
	if (ufs_call_cal(ufs, 0, ufs_cal_post_link))
		goto out;
#else
	if (ufs_post_link(ufs))
		goto out;
#endif

	printf("UFS link established\n");

	/* 5. update active lanes */
	if (ufs_update_active_lane(ufs))
		goto out;

	/* 6. misc hci setup for NOP and fDeviceinit */
	ufs_vendor_setup(ufs);

	/* 7. NOP and fDeviceinit */
	if (ufs_end_boot_mode(ufs))
		goto out;

	printf("UFS device initialized\n");

	/* 7-1. notify a knowledge of using 26MHz */
	if (ufs_ref_clk_setup(ufs))
		goto out;

	/* 8. Check a number of connected lanes */
	if (ufs_check_2lane(ufs)) {
		printf("UFS check 2lane Fail\n");
		goto out;
	}

	/* 9. pre pmc */
	pmd->gear = ufs->gear;
	pmd->mode = UFS_POWER_MODE;
	pmd->hs_series = UFS_RATE;

#ifdef UFS_CAL_DIR
	ufs->cal_param.pmd = pmd;
	if (ufs_call_cal(ufs, 0, ufs_cal_pre_pmc))
		goto out;
#else
	if (ufs_pre_gear_change(ufs, pmd))
		goto out;
#endif

	/* 10. pmc (power mode change) */
	if (ufs_pmc_common(ufs, pmd)) {
		if (boot_with_usb())
			ufs_sec_nad_err_logging(UFS_ERR_POWER_MODE_CHANGE, UFS_NAD_INIT_ERR);
		goto out;
	}

	/* 11. update active lanes */
	if (ufs_update_active_lane(ufs))
		goto out;
#ifdef UFS_CAL_DIR
	if (ufs_call_cal(ufs, 0, ufs_cal_post_pmc))
		goto out;
#else
	/* 12. post pmc */
	if (ufs_post_gear_change(ufs))
		goto out;
#endif

	printf("UFS Power mode change: M(%d)G(%d)L(%d)HS-series(%d)\n",
			(pmd->mode & 0xF), pmd->gear, pmd->lane, pmd->hs_series);
	res = 0;
out:
	return res;
}

static void ufs_init_mem(struct ufs_host *ufs)
{
	wp_dat_buf = malloc(0x2000);

	ufs_debug("cmd_desc_addr : %p\n", ufs->cmd_desc_addr);
	ufs_debug("\tresponse_upiu : %p\n", &ufs->cmd_desc_addr->response_upiu);
	ufs_debug("\tprd_table : %p (size=%lx)\n", ufs->cmd_desc_addr->prd_table,
		  sizeof(ufs->cmd_desc_addr->prd_table));
	ufs_debug("\tsizeof upiu : %lx\n", sizeof(struct ufs_upiu));

	memset(ufs->cmd_desc_addr, 0x00, UFS_NUTRS * sizeof(struct ufs_cmd_desc));

	ufs_debug("utrd_addr : %p\n", ufs->utrd_addr);
	memset(ufs->utrd_addr, 0x00, UFS_NUTRS * sizeof(struct ufs_utrd));

	ufs->utrd_addr->cmd_desc_addr_l = (u64)(ufs->cmd_desc_addr);
	ufs->utrd_addr->rsp_upiu_off = (u16)(offsetof(struct ufs_cmd_desc, response_upiu));
	ufs->utrd_addr->rsp_upiu_len = (u16)(ALIGNED_UPIU_SIZE);

	writel((u64)ufs->utmrd_addr, VM_MEM(ufs->ioaddr + REG_UTP_TASK_REQ_LIST_BASE_L));
	writel(0, VM_MEM(ufs->ioaddr + REG_UTP_TASK_REQ_LIST_BASE_H));

	writel((u64)ufs->utrd_addr, VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_LIST_BASE_L));
	writel(0, VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_LIST_BASE_H));
}

static int ufs_identify_device(struct ufs_host *ufs)
{
	int boot_lun_en;
	int i;
	int res;
	u32 dExtendedUFSFeaturesSupport;

	u16 wSpecVersion;
	u8 bFFUTimeout;

	ufs->lun = 0;
	/* read device descriptor */
	res = ufs_utp_query_retry(ufs, DESC_R_DEVICE_DESC, 0);
	if (res) {
		printf("UFS Check Read Device Desc Error: %d", res);
		goto end;
	}

	/* show device information */
	printf("\n");
	wSpecVersion = be16_to_cpu(ufs->device_desc.wSpecVersion);
	bFFUTimeout = ufs->device_desc.bFFUTimeout;

	printf("UFS device wSpecVersion: 0x%04x\n", wSpecVersion);
	print_lcd_update(FONT_GREEN, FONT_BLACK,
			"UFS device wSpecVersion: 0x%04x\n", wSpecVersion);
	printf("UFS device bFFUTimeout: 0x%08x, but use %u\n",
			bFFUTimeout, FFU_TIMEOUT);
	print_lcd_update(FONT_GREEN, FONT_BLACK,
			"UFS device bFFUTimeout: 0x%08x, but use %u\n",
			bFFUTimeout, FFU_TIMEOUT);

	printf("device bLength: 0x%04x\n", ufs->device_desc.bLength);
	printf("device bNumberLU: 0x%04x\n", ufs->device_desc.bNumberLU);
	printf("device bBootEnable: 0x%04x\n", ufs->device_desc.bBootEnable);
	printf("device wManufacturerID: 0x%04x\n", be16_to_cpu(ufs->device_desc.wManufacturerID));
	printf("device bUD0BaseOffset: 0x%04x\n", ufs->device_desc.bUD0BaseOffset);
	printf("device bUDConfigPlength: 0x%04x\n", ufs->device_desc.bUDConfigPlength);
	printf("device dNumSharedWriteBoosterBufferAllocUnits: 0x%08x\n",
			ufs->device_desc.dNumSharedWriteBoosterBufferAllocUnits);

	if (!ufs->device_desc.bUDConfigPlength || !ufs->device_desc.bUD0BaseOffset) {
		printf("UFS err during setup congif_desc, device_desc.bUD0BaseOffset : %d, device_desc.bUDConfigPlength : %d\n",
				ufs->device_desc.bUD0BaseOffset, ufs->device_desc.bUDConfigPlength);
		res = RET_FAILURE;
		goto end;
	}

	/* read geometry descriptor */
	res = ufs_utp_query_retry(ufs, DESC_R_GEOMETRY_DESC, 0);
	if (res) {
		printf("UFS Check Read Geometry Desc Error: %d", res);
		goto end;
	}

	/* Currently, check if WB is supported */
	dExtendedUFSFeaturesSupport = ___swab32(ufs->device_desc.dExtendedUFSFeaturesSupport);
	if (dExtendedUFSFeaturesSupport & 0x100) {
		printf("UFS supports WB feature, 0x%x\n", ufs->device_desc.dExtendedUFSFeaturesSupport);
		ufs->support_wb = 1;
		ufs->wb_buf_type = ufs->geometry_desc.bSupportedWriteBoosterBufferTypes;
		if (!ufs->geometry_desc.bSupportedWriteBoosterBufferUserSpaceReductionTypes) {
			printf("UFS doesn't support no user space reduction, provision fail\n");
			goto end;
		}
	} else {
		printf("UFS device doesn't support write booster feature, 0x%x\n", dExtendedUFSFeaturesSupport);
		ufs->support_wb = 0;
	}

	/* read bootluen attribute */
	res = ufs_utp_query_retry(ufs, ATTR_R_BOOTLUNEN, 0);
	if (res)
		goto end;

	boot_lun_en = ufs->attributes.arry[UPIU_ATTR_ID_BOOTLUNEN];
	if (boot_lun_en == 0) {
		printf("Boot LUN is disabled\n");

		/*
		 * In case of kioxia, hynix initial boot, boot LU can be disabled.
		 * So it doesn't need to return non-zero value. Ignore return value and do provision.
		 */
		if (!boot_with_usb())
			res = -1;
		goto end;
	}

	for (i = 0; i < 8; i++) {
		ufs_query_params[DESC_R_UNIT_DESC][3] = i;
		res = ufs_utp_query_retry(ufs, DESC_R_UNIT_DESC, i);
		if (res)
			goto end;
		if (boot_lun_en == ufs->unit_desc[i].bBootLunID) {
			printf("Boot LUN is #%d, bBootLunID:%d\n", i, ufs->unit_desc[i].bBootLunID);
		}
	}

	/* read rpmb unit descriptor */
	res = ufs_utp_query_retry(ufs, DESC_R_RPMB_UNIT_DESC, UFS_RPMB_UNIT_DESCRIPTOR_INDEX);
	if (res)
		goto end;
end:
	return res;
}

static void ufs_disable_ufsp(struct ufs_host *ufs)
{
	writel(0x0, VM_MEM(ufs->fmp_addr + UFSP_UPSBEGIN0));
	writel(0xffffffff, VM_MEM(ufs->fmp_addr + UFSP_UPSEND0));
	writel(0xff, VM_MEM(ufs->fmp_addr + UFSP_UPLUN0));
	writel(0xf1, VM_MEM(ufs->fmp_addr + UFSP_UPSCTRL0));
}

/*
 * This function shall be called at least once.
 * Expept for the case that LUs are not configured properly,
 * this function execution makes devices to prepare IO process
 */
static int ufs_init_host(int host_index, struct ufs_host *ufs)
{
	if (host_index) {
		printf("Currently multi UFS host is not supported!\n");
		return -1;
	}

	sprintf(ufs->host_name,"ufs%d", host_index);
	ufs->host_index = host_index;

	if (boot_with_jtag()) {
		/* It boots by T32. set SMU as by-passed */
		ufs_disable_ufsp(ufs);
	}

	/* AP specific UFS host init */
	if (ufs_set_init(host_index, ufs))
		goto out;
#ifdef UFS_CAL_DIR
	ufs_map_vs_regions(ufs);
#endif

	/* Read capabilities registers */
	ufs->capabilities = readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_CAPABILITIES));
	ufs->ufs_version = readl(VM_MEM(ufs->ioaddr + REG_UFS_VERSION));

	printf("%s\n\tcaps(0x%p) 0x%08x\n\tver(0x%p)  0x%08x\n\tPID(0x%p)  0x%08x\n\tMID(0x%p)  0x%08x\n",
		ufs->host_name, ufs->ioaddr + REG_CONTROLLER_CAPABILITIES, ufs->capabilities,
		ufs->ioaddr + REG_UFS_VERSION, ufs->ufs_version, ufs->ioaddr + REG_CONTROLLER_PID,
		readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_PID)),
		ufs->ioaddr + REG_CONTROLLER_MID, readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_MID)));
	print_lcd(FONT_WHITE, FONT_BLACK,
		"%s\n\tcaps(0x%p) 0x%08x\n\tver(0x%p)  0x%08x\n\tPID(0x%p)  0x%08x\n\tMID(0x%p)  0x%08x\n",
		ufs->host_name, ufs->ioaddr + REG_CONTROLLER_CAPABILITIES, ufs->capabilities,
		ufs->ioaddr + REG_UFS_VERSION, ufs->ufs_version, ufs->ioaddr + REG_CONTROLLER_PID,
		readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_PID)),
		ufs->ioaddr + REG_CONTROLLER_MID, readl(VM_MEM(ufs->ioaddr + REG_CONTROLLER_MID)));

	ufs_init_mem(ufs);

#ifdef UFS_CAL_DIR
	/* init cal */
	ufs->cal_param.handle = &ufs->handle;
	if (CONFIG_UFS_BOARD_TYPE)
		ufs->cal_param.board = BRD_UNIV;
	else
		ufs->cal_param.board = BRD_SMDK;
	if (ufs_call_cal(ufs, 1, ufs_cal_init))
		goto out;
#else
	if (ufs_init_cal(ufs, host_index))
		goto out;
#endif

	/* dump */
	ufs->debug.sfr = ufs_log_sfr;
	ufs->debug.attr = ufs_log_attr;

	return 0;

 out:
	return -1;
}

static int ufs_read_descs_and_bootlun_attr(struct ufs_host *ufs)
{
	int res;

	ufs->lun = 0;
	res = ufs_utp_query_retry(ufs, DESC_R_DEVICE_DESC, 0);

	if (res) {
		printf("UFS Check Read Device Desc Error: %d", res);
		goto out;
	}
	res = ufs_utp_query_retry(ufs, ATTR_R_BOOTLUNEN, 0);
	if (res) {
		printf("UFS Check Read BootlunEn Attr Error: %d", res);
		goto out;
	}
	res = ufs_utp_query_retry(ufs, DESC_R_CONFIG_DESC, 0);
	if (res) {
		printf("UFS Check Read Config Desc Error: %d", res);
		goto out;
	}
out:
	res = (!res) ? RET_SUCCESS : RET_INVALID;
	return res;
}

/*
 * EXTERNAL FUNCTION: ufs_init
 *
 * This is called at boot time to initialize UFS driver and
 * enumerate all the attached Logical Units.
 */
status_t ufs_init(int mode)
{

	int r = 0, i;
	int rst_cnt = 0;

	// TODO:
#if 0
	if ((mode == 2) && (exynos_boot_mode() != BOOT_UFS)) {
		dprintf(INFO, " Not UFS boot mode. Init UFS manually.\n");
		return r;
	}
#endif
	ufs_number_of_lus = 0;
	for (i = 0; i < SCSI_MAX_DEVICE; i++) {
		if (SEC_LU_conf->unit[i].bLUEnable)
			ufs_number_of_lus++;
	}

	for (i = 0; i < SCSI_MAX_INITIATOR; i++) {
		/* Initialize host */
		r = ufs_init_host(i, _ufs[i]);
		if (r) {
			if (boot_with_usb())
				ufs_sec_nad_err_logging(UFS_ERR_INIT_HOST, UFS_NAD_INIT_ERR);
			goto out;
		}

		/* Establish interface */
		do {
			r = ufs_init_interface(_ufs[i]);
			if (!r)
				break;
			rst_cnt++;
			printf("UFS: remained retries for init : %d\n", rst_cnt);
		} while (rst_cnt < 3);

		if (r) {
			if (boot_with_usb())
				ufs_sec_nad_init_check(_ufs[i], PRE_INIT, r);
			goto out;
		}

		/* Check boot LUs exist only in normal boot, not edl. */
		r = ufs_identify_device(_ufs[i]);
		if (r && !boot_with_usb())
			goto out;

		/* SCSI device enumeration */
		scsi_scan(ufs_dev[i], 0, ufs_number_of_lus, scsi_exec, NULL, 128);
		scsi_scan(&ufs_dev_rpmb, LUN_DEV_RPMB, 0, scsi_exec, "rpmb", 128);
		scsi_scan_ssu(&ufs_dev_ssu, LUN_DEV_SSU, scsi_exec, (get_sdev_t *)scsi_get_ssu_sdev);

		_ufs[i]->wManufactureID = ((_ufs[0]->device_desc.wManufacturerID) & 0xff00) >> 8;

		_ufs[i]->active = 1;

		ufs_get_serial_number();
		ufs_get_product_name();
	}

out:
	if (r) {
		ufs_err("fail to ufs_init(%d): %d\n", i, r);
		if (boot_with_usb())
			ufs_sec_nad_init_check(_ufs[i], POST_INIT, r);
	}

	/*
	 * Current host is zero by default after preparing to read and write
	 * because we assume that system boot requires host #0
	 */
	_ufs_curr_host = 0;

	return r;
}

/*
 * EXTERNAL FUNCTION: ufs_alloc_memory
 *
 * This function shall be called only once. These memory would be used
 * permantely in bootloader lifcycle, so we don't need to free memory
 */
int ufs_alloc_memory()
{
	struct ufs_host *ufs;
	int r = -1, i;
	size_t len;

	for (i = 0; i < SCSI_MAX_INITIATOR; i++) {
		_ufs_curr_host = i;

		/* Allocation for host */
		len = sizeof(struct ufs_host);
		if (!(_ufs[i] = malloc(len)))
			goto end;
		ufs = _ufs[i];
		memset(_ufs[i], 0x00, sizeof(struct ufs_host));

		/* Allocation for CAL */
#ifdef UFS_CAL_DIR
		memset(&ufs->cal_param, 0x00, sizeof(struct ufs_cal_param));
#else
		len = sizeof(struct ufs_cal_param);
		if (!(ufs->cal_param = malloc(len)))
			goto end;
#endif

		/* Allocation for descriptor */
		len = UFS_NUTRS * sizeof(struct ufs_cmd_desc);
		if (!(ufs->cmd_desc_addr = memalign(0x1000, len))) {
			printf("UFS: %s: cmd_desc_addr memory alloc error!!!\n", __func__);
			goto end;
		}
		if ((u64)(ufs->cmd_desc_addr) & 0xfff) {
			printf("UFS: %s: allocated cmd_desc_addr memory align error!!!\n", __func__);
			goto end;
		}

		len = UFS_NUTRS * sizeof(struct ufs_utrd);
		if (!(ufs->utrd_addr = memalign(0x1000, len))) {
			printf("UFS: %s: utrd_addr memory alloc error!!!\n", __func__);
			goto end;
		}
		if ((u64)(ufs->utrd_addr) & 0xfff) {
			printf("UFS: %s: allocated utrd_addr memory align error!!!\n", __func__);
			goto end;
		}

		len = UFS_NUTMRS * sizeof(struct ufs_utmrd);
		ufs->utmrd_addr = memalign(0x1000, len);
		if (!ufs->utmrd_addr) {
			dprintf(INFO, "utmrd_addr memory alloc error!!!\n");
			goto end;
		}
		if ((u64) (ufs->utmrd_addr) & 0xfff) {
			dprintf(INFO, "allocated utmrd_addr memory align error!!!\n");
			goto end;
		}

		/* Allocation for device enumeration */
		len = sizeof(scsi_device_t) * SCSI_MAX_DEVICE;
		if (!(ufs_dev[i] = (scsi_device_t *)malloc(len))) {
			printf("UFS: %s: ufs_dev memory allocation failed\n", __func__);
			goto end;
		}
	}

	r = 0;
end:
	if (r != 0)
		printf("##### LK memory allocation fails !!! #####\n");

	return r;
}

/*
 * EXTERNAL FUNCTION: send_request_descriptor
 *
 * Function used by JEDEC UFS Test
 * Send either UTP Transfer Request Descriptor (UTRD) or Task Management
 * Request Descriptor (UTMRD). Type identified via command type.
 */
int send_request_descriptor(void *rd, u8 command_type, u64 timeout)
{
	struct ufs_host *ufs = get_cur_ufs_host();
	/* existing_descriptor is used to current backup utrd/utmrd. It is sized to
	 * the maximum of the two */
	u8 existing_descriptor[sizeof(struct ufs_utmrd)];
	int err = UFS_IN_PROGRESS;

	switch (command_type) {
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_COMMAND:
	case UPIU_TRANSACTION_QUERY_REQ:
		memcpy(existing_descriptor, ufs->utrd_addr, sizeof(struct ufs_utrd));
		memcpy(ufs->utrd_addr, rd, sizeof(struct ufs_utrd));
		arch_clean_invalidate_cache_range((addr_t)ufs->utrd_addr, sizeof(struct ufs_utrd));
		break;
	case UPIU_TRANSACTION_TASK_REQ:
		memcpy(existing_descriptor, ufs->utmrd_addr, sizeof(struct ufs_utmrd));
		memcpy(ufs->utmrd_addr, rd, sizeof(struct ufs_utmrd));
		arch_clean_invalidate_cache_range((addr_t)ufs->utmrd_addr, sizeof(struct ufs_utmrd));
		break;
	}

	switch (command_type) {
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_QUERY_REQ:
		writel(0x0, VM_MEM(ufs->vs_addr + VS_UTRL_NEXUS_TYPE));
		break;
	case UPIU_TRANSACTION_COMMAND:
		writel(0xFFFFFFFF, VM_MEM(ufs->vs_addr + VS_UTRL_NEXUS_TYPE));
	case UPIU_TRANSACTION_TASK_REQ:
	default:
		break;
	}

	switch (command_type) {
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_COMMAND:
	case UPIU_TRANSACTION_QUERY_REQ:
		writel(1, VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_DOOR_BELL));
		break;
	case UPIU_TRANSACTION_TASK_REQ:
		writel(1, VM_MEM(ufs->ioaddr + REG_UTP_TASK_REQ_DOOR_BELL));
		break;
	}


	while (timeout) {
		u32 intr_stat = readl(VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS));

		switch (command_type) {
		case UPIU_TRANSACTION_NOP_OUT:
		case UPIU_TRANSACTION_COMMAND:
		case UPIU_TRANSACTION_QUERY_REQ:
			if ((intr_stat & UTP_TRANSFER_REQ_COMPL) &&
				!(readl(VM_MEM(ufs->ioaddr + REG_UTP_TRANSFER_REQ_DOOR_BELL)) & 1))
					err = UFS_NO_ERROR;
			break;
		case UPIU_TRANSACTION_TASK_REQ:
			if ((intr_stat & UTP_TASK_REQ_COMPL) &&
				!(readl(VM_MEM(ufs->ioaddr + REG_UTP_TASK_REQ_DOOR_BELL)) & 1)) {
					err = UFS_NO_ERROR;
				}
			break;
		}

		/* Fatal error case */
		if (intr_stat & INT_FATAL_ERRORS) {
			printf("UFS: FATAL ERROR 0x%08x\n", intr_stat);
			err = UFS_ERROR;
		}

		if (err != UFS_IN_PROGRESS) {
			break;
		}

		udelay(1);
		timeout--;
	}

	if (!timeout) {
		err = UFS_TIMEOUT;
		printf("UFS: TIMEOUT\n");
	}

	writel(readl(VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS)), VM_MEM(ufs->ioaddr + REG_INTERRUPT_STATUS));

	switch (command_type) {
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_QUERY_REQ:
		writel((readl(ufs->ioaddr + 0x140) | 0x01), VM_MEM(ufs->ioaddr + 0x140));
		break;
	case UPIU_TRANSACTION_COMMAND:
	case UPIU_TRANSACTION_TASK_REQ:
	default:
		break;
	}

	switch (command_type) {
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_COMMAND:
	case UPIU_TRANSACTION_QUERY_REQ:
		memcpy(rd, ufs->utrd_addr, sizeof(struct ufs_utrd));
		memcpy(ufs->utrd_addr, existing_descriptor, sizeof(struct ufs_utrd));
		arch_clean_invalidate_cache_range((addr_t)ufs->utrd_addr, sizeof(struct ufs_utrd));
		break;
	case UPIU_TRANSACTION_TASK_REQ:
		memcpy(rd, ufs->utmrd_addr, sizeof(struct ufs_utmrd));
		memcpy(ufs->utmrd_addr, existing_descriptor, sizeof(struct ufs_utmrd));
		arch_clean_invalidate_cache_range((addr_t)ufs->utmrd_addr, sizeof(struct ufs_utmrd));
		break;
	}

	return err;
}

static u32 __get_max_wb_alloc_units(struct ufs_host *ufs)
{
	return ufs->geometry_desc.dWriteBoosterBufferMaxNAllocUnits;
}

static void ufs_set_config_hdr(struct ufs_host *ufs)
{
	struct ufs_config_desc_header *src_hdr;
	struct ufs_config_desc_header *tgt_hdr;
	u8 wb_buf_type;
	u32 wb_alloc_units;

	src_hdr = &LU_conf->header;
	tgt_hdr = &ufs->config_desc.header;

	/* user conf */
	tgt_hdr->bConfDescContinue = src_hdr->bConfDescContinue;
	tgt_hdr->bBootEnable = src_hdr->bBootEnable;
	tgt_hdr->bDescrAccessEn = src_hdr->bDescrAccessEn;
	tgt_hdr->bInitPowerMode = src_hdr->bInitPowerMode;
	tgt_hdr->bHighPriorityLUN = src_hdr->bHighPriorityLUN;
	tgt_hdr->bSecureRemovalType = src_hdr->bSecureRemovalType;
	tgt_hdr->bInitActiveICCLevel = src_hdr->bInitActiveICCLevel;
	tgt_hdr->wPeriodicRTCUpdate = cpu_to_be16(src_hdr->wPeriodicRTCUpdate);

	if (ufs->support_wb) {
		/* user conf */
		tgt_hdr->bWriteBoosterBufferNoUserSpaceReductionEn =
			src_hdr->bWriteBoosterBufferNoUserSpaceReductionEn;
		/*
		 * We always use LU dedicated mode,
		 * except when the mode isn't supported. Moreover, some devices
		 * including UFS 2.2 seems to work only in LU dedicated mode.
		 */
		if (ufs->wb_buf_type == 1) {
			wb_buf_type = ufs->wb_buf_type;
			wb_alloc_units = __get_max_wb_alloc_units(ufs);
		} else {
			wb_buf_type = 0;
			wb_alloc_units = 0;
		}
		tgt_hdr->dNumSharedWriteBoosterBufferAllocUnits =
			cpu_to_be32(wb_alloc_units);
		tgt_hdr->bWriteBoosterBufferType = wb_buf_type;
	}
}

/*
 * The expression to calculate the number of allocation unit
 *
 * CEILING((capacity in bytes * CapacityAdjFactor)/
 *         (bAllocationUnitSize * dSegmentSize * 512)
 */
static u32 ufs_get_alloc_unit_number(struct ufs_host *ufs, u32 lun,
					u32 sum_in_units)
{
	struct ufs_unit_desc_param *param = &LU_conf->unit[lun];
	u32 units;
	u64 total;
	/*
	 * dNumAllocUnits in source code doesn't represent the number in
	 * allocation units. It's Megabytes, so you need to multiply 2^20
	 * to get the byte size.
	 */
	u64 bytes = param->dNumAllocUnits * 1024 * 1024;
	/*
	 * Currently we always use Enhanced memory type 1 for boot LUs
	 */
	u32 capacityadjfactor =
		be16_to_cpu(ufs->geometry_desc.wEnhanced1CapAdjFac) / 256;
	/*
	 * allocation size is expressed in number of segments and
	 * segment size is expressed in 512 byes.
	 */
	u64 unit_in_byte = ufs->geometry_desc.bAllocationUnitSize *
			be32_to_cpu(ufs->geometry_desc.dSegmentSize) * 512;

	/*
	 * We calculate total size in allocation unit to
	 * give all the remained area to LU #0 and can get
	 * the remained size in allocation unit by decreasing as mush as
	 * the total size of other LUs.
	 *
	 * qTotalRawDeviceCapacity is expressed in 512 bytes.
	 */
	if (lun == 0) {
		total = (u64)be32_to_cpu(ufs->geometry_desc.qTotalRawDeviceCapacity_h) << 32;
		total += be32_to_cpu(ufs->geometry_desc.qTotalRawDeviceCapacity_l);
		units = (u32)(total / be32_to_cpu(ufs->geometry_desc.dSegmentSize) /
			ufs->geometry_desc.bAllocationUnitSize - sum_in_units);
	} else if (param->bMemoryType > 0)
		units = (u32)((bytes * capacityadjfactor + unit_in_byte - 1) /
							unit_in_byte);
	else
		units = (u32)((bytes + unit_in_byte - 1) / unit_in_byte);

	return units;
}

static void ufs_set_config_unit(struct ufs_host *ufs, u32 lun, u32 *sum_in_alloc_units)
{
	struct ufs_unit_desc_param *src;
	struct ufs_unit_desc_param *tgt;

	u32 dNumAllocUnits;

	tgt = &ufs->config_desc.unit[lun];
	src = &LU_conf->unit[lun];

	/* user conf */
	tgt->bLUEnable = src->bLUEnable;
	tgt->bBootLunID = src->bBootLunID;
	tgt->bLUWriteProtect = src->bLUWriteProtect;
	tgt->bMemoryType = src->bMemoryType;
	tgt->bDataReliability = src->bDataReliability;
	tgt->bLogicalBlockSize = src->bLogicalBlockSize;
	tgt->bProvisioningType = src->bProvisioningType;
	tgt->wContextCapabilities = cpu_to_be16(src->wContextCapabilities);

	*sum_in_alloc_units += dNumAllocUnits = ufs_get_alloc_unit_number(ufs, lun, *sum_in_alloc_units);
	tgt->dNumAllocUnits = cpu_to_be32(dNumAllocUnits);
	printf("ufs->config_desc.unit[%d].dNumAllocUnits:%02x\n", lun, tgt->dNumAllocUnits);

	/*
	 * LU dedicated mode: max value only for LU #0
	 * Shared mode: All zero
	 */
	dNumAllocUnits = 0;
	if (ufs->support_wb && ufs->wb_buf_type != 1 && lun == 0) {
		dNumAllocUnits = __get_max_wb_alloc_units(ufs);
		printf("dLUNumWriteBoosterBufferAllocUnits for LU #0: 0x%x\n", be32_to_cpu(dNumAllocUnits));
	}
	tgt->dLUNumWriteBoosterBufferAllocUnits = dNumAllocUnits;
}

static int ufs_check_config_desc(struct ufs_host *ufs)
{
	int lun = 0;
	u32 sum_in_units = 0;
	int boot_lun_en, res;
	struct ufs_config_desc_header *src_hdr;
	struct ufs_config_desc_header *tgt_hdr;
	struct ufs_unit_desc_param *src;
	struct ufs_unit_desc_param *tgt;
	u32 src_in_units;
	u32 tgt_in_units;

	res = ufs_read_descs_and_bootlun_attr(ufs);
	if (res)
		goto fail;
	else if (!ufs->device_desc.bUDConfigPlength || !ufs->device_desc.bUD0BaseOffset) {
		printf("UFS err during setup congif_desc, device_desc.bUD0BaseOffset : %d, device_desc.bUDConfigPlength : %d\n",
				ufs->device_desc.bUD0BaseOffset, ufs->device_desc.bUDConfigPlength);
		goto fail;
	}
	res = RET_FAILURE;

	/* Check bBootLunEn (attribute) */
	boot_lun_en = ufs->attributes.arry[UPIU_ATTR_ID_BOOTLUNEN];
	if (boot_lun_en == 0) {
		printf("UFS bootLU is not enabled\n");
		goto fail;
	}

	/* Check Configuration descriptors (descriptor) */
	src_hdr = &ufs->config_desc.header;
	tgt_hdr = &LU_conf->header;
	if (src_hdr->bLength != (ufs->device_desc.bUD0BaseOffset
				+ (8 * ufs->device_desc.bUDConfigPlength)))
		printf("UFS_Config_Desc_Header bLength error\n");
	else if (src_hdr->bDescriptorType != tgt_hdr->bDescriptorType)
		printf("UFS_Config_Desc_Header bDescriptorType error\n");
	else if (src_hdr->bConfDescContinue != tgt_hdr->bConfDescContinue)
		printf("UFS_Config_Desc_Header bConfDescContinue error\n");
	else if (src_hdr->bBootEnable != tgt_hdr->bBootEnable)
		printf("UFS_Config_Desc_Header bBootEnable error\n");
	else if (src_hdr->bDescrAccessEn != tgt_hdr->bDescrAccessEn)
		printf("UFS_Config_Desc_Header bDescrAccessEn error\n");
	else if (src_hdr->bInitPowerMode != tgt_hdr->bInitPowerMode)
		printf("UFS_Config_Desc_Header bInitPowerMode error\n");
	else if (src_hdr->bHighPriorityLUN != tgt_hdr->bHighPriorityLUN)
		printf("UFS_Config_Desc_Header bHighPriorityLUN error\n");
	else if (src_hdr->bSecureRemovalType != tgt_hdr->bSecureRemovalType)
		printf("UFS_Config_Desc_Header bSecureRemovalType error\n");
	else if (src_hdr->bInitActiveICCLevel != tgt_hdr->bInitActiveICCLevel)
		printf("UFS_Config_Desc_Header bInitActiveICCLevel error\n");
	else if (src_hdr->wPeriodicRTCUpdate != be16_to_cpu(tgt_hdr->wPeriodicRTCUpdate))
		printf("UFS_Config_Desc_Header wPeriodicRTCUpdate error\n");

	if (ufs->support_wb) {
		if (src_hdr->bWriteBoosterBufferNoUserSpaceReductionEn
				!= tgt_hdr->bWriteBoosterBufferNoUserSpaceReductionEn) {
			printf("UFS_Config_Desc_Header bWriteBoosterBufferNoUserSpaceReductionEn error\n");
			goto fail;
		}

		if (ufs->config_desc.unit[0].dLUNumWriteBoosterBufferAllocUnits
				!= ufs->geometry_desc.dWriteBoosterBufferMaxNAllocUnits)
			printf("dLUNumWriteBoosterBufferAllocUnits error at LU%d  %d, %d\n",
				lun, ufs->geometry_desc.dWriteBoosterBufferMaxNAllocUnits,
				ufs->config_desc.unit[0].dLUNumWriteBoosterBufferAllocUnits);
	}

	for (lun = 7, src = &ufs->config_desc.unit[lun], tgt = &LU_conf->unit[lun]; lun >= 0; lun--, src--, tgt--) {
		src_in_units = be32_to_cpu(src->dNumAllocUnits);
		if (tgt->bLUEnable != src->bLUEnable) {
			printf("bLUEnable error at LU%d  %d, %d\n",
				lun, tgt->bLUEnable, src->bLUEnable);
			goto fail;
		}

		if (tgt->bBootLunID != src->bBootLunID) {
			printf("bBootLunID error at LU%d  %d, %d\n",
				lun, tgt->bBootLunID, src->bBootLunID);
			goto fail;
		}

		if (tgt->bLUWriteProtect != src->bLUWriteProtect) {
			printf("bLUWriteProtect error at LU%d  %d, %d\n",
				lun, tgt->bLUWriteProtect, src->bLUWriteProtect);
			goto fail;
		}

		if (tgt->bMemoryType != src->bMemoryType) {
			printf("bMemoryType error at LU%d  %d, %d\n",
				lun, tgt->bMemoryType, src->bMemoryType);
			goto fail;
		}

		sum_in_units += tgt_in_units = ufs_get_alloc_unit_number(ufs, lun, sum_in_units);
		if (src_in_units != tgt_in_units) {
			printf("dNumAllocUnits error at LU%d: (src= 0x%x, tgt= 0x%x)\n",
					lun, src_in_units, tgt_in_units);
			goto fail;
		}

		if (tgt->bDataReliability != src->bDataReliability) {
			printf("bDataReliability error at LU%d  %d, %d\n",
				lun, tgt->bDataReliability, src->bDataReliability);
			goto fail;
		}

		if (tgt->bLogicalBlockSize != src->bLogicalBlockSize) {
			printf("bLogicalBlockSize error at LU%d  %d, %d\n",
				lun, tgt->bLogicalBlockSize, src->bLogicalBlockSize);
			goto fail;
		}

		if (tgt->bProvisioningType != src->bProvisioningType) {
			printf("bProvisioningType error at LU%d  %d, %d\n",
				lun, tgt->bProvisioningType, src->bProvisioningType);
			goto fail;
		}

		if (tgt->wContextCapabilities != be16_to_cpu(src->wContextCapabilities)) {
			printf("wContextCapabilities error at LU%d  %d, %d\n",
				lun, tgt->wContextCapabilities, src->wContextCapabilities);
			goto fail;
		}
	}

	res = RET_SUCCESS;
fail:
	printf("UFS CHECK LU conf: %s\n",
			(res == RET_SUCCESS) ? ret_token[0] : ret_token[1]);
	return res;
}

/*
 * EXTERNAL FUNCTION: ufs_set_configuration_descriptor
 *
 * This is called at boot time to check that LU configuration is done
 * with expected values and, if not, do it.
 *
 * Return values
 * 0: no error
 * 1: LU configuration done
 * others: uncorrectable error
 */
int ufs_set_configuration_descriptor(void)
{
	int lun;
	int ret = RET_INVALID;
	int ret_out;
	int retry = 0;
	int is_conf_hdr = 0;
	int is_conf = 0;
	u32 sum_in_units;
	struct ufs_host *ufs = get_cur_ufs_host();
	if (!ufs)
		goto out;

	/* Check if the values are the same with code */
	while (RET_SUCCESS != (ret = ufs_check_config_desc(ufs)) && retry != 3) {
		/* If query fails, just retry query */
		if (ret == RET_INVALID)
			continue;

		/* start lu config */
		is_conf = 0;
		printf("UFS SET LU conf: trying %d...\n", retry++);

		/* set header at once */
		if (is_conf_hdr++ == 0)
			ufs_set_config_hdr(ufs);

		/* set unit desc params */
		sum_in_units = 0;
		for (lun = 7; lun >= 0; lun--)
			ufs_set_config_unit(ufs, lun, &sum_in_units);

		/* update configuration descriptor */
		ret = ufs_utp_query_retry(ufs, DESC_W_CONFIG_DESC, 0);
		if (ret) {
			printf("UFS LU config: Descriptor write query fail with %d\n", ret);
			continue;
		}

		/* enable boot lu A */
		ufs->attributes.arry[UPIU_ATTR_ID_BOOTLUNEN] = 0x01;
		ret = ufs_utp_query_retry(ufs, ATTR_W_BOOTLUNEN, 0);
		if (ret) {
			printf("UFS LU config: BootLUNEN setting fail with %d\n", ret);
			continue;
		}

		/* guaranteed that LU config sequence is done w/o any error */
		is_conf = 1;

		u_delay(1000*1000);
	}

out:
	/*
	 * The caller determines if ufs_init should be invoked again
	 * with the value of ret_out. 1 means it's needed.
	 */
	ret_out = 0;
	if (ret == RET_SUCCESS) {
		if (is_conf) {
			printf("UFS SET LU conf: %s !!!\n", ret_token[0]);
			/* remove enumerated bdevs*/
			scsi_exit("scsi");
			/* to identify the result from outside */
			ret_out = 1;
		}
	} else if (ret == RET_FAILURE) {
		printf("UFS SET LU conf: %s !!!\n", ret_token[1]);
		print_ufs_information(ufs);
		ret_out = 2;
	}

	return ret_out;
}

const u16 *ufs_get_oem_id(void)
{
	return ufs_get_string(STRING_OEM_ID);
}

const u16 *ufs_get_serial_number(void)
{
	return ufs_get_string(STRING_SERIAL_NUMBER);
}

const u16 *ufs_get_product_name(void)
{
	return ufs_get_string(STRING_PRODUCT_NAME);
}

const u16 *ufs_get_manufacturer_name(void)
{
	return ufs_get_string(STRING_MANUFACTURER_NAME);
}

const u16 *ufs_get_product_revision(void)
{
	return ufs_get_string(STRING_PRODUCT_REVISION);
}

u16 ufs_get_protocol_version(void)
{
	struct ufs_host *ufs = get_cur_ufs_host();
	if (!ufs->device_desc.bLength) {
		printf("device descriptor must be read first\n");
		return 0;
	}

	return be16_to_cpu(ufs->device_desc.wSpecVersion);
}

u64 ufs_get_total_capacity_bytes(void)
{
	struct ufs_host *ufs = get_cur_ufs_host();
	if (!ufs->geometry_desc.bLength) {
		printf("geometry descriptor must be read first\n");
		return 0;
	}

	u64 cap_low = be32_to_cpu(ufs->geometry_desc.qTotalRawDeviceCapacity_l);
	u64 cap_high = be32_to_cpu(ufs->geometry_desc.qTotalRawDeviceCapacity_h);

	return (cap_low | cap_high << 32) * 512;
}

int ufs_reinit_if(enum scsi_ufs_init_op op)
{
	struct ufs_host *ufs;
	int res = -1;
	u8 status;

	/* check */
	if (op >= SCSI_UFS_INIT_MAX)
		return res;
	ufs = get_cur_ufs_host();
	if (!ufs)
		return res;

	/* init */
	res = ufs_init_interface(ufs);

	/* post */
	if (res == 0 && op == SCSI_UFS_INIT_FFU) {
		res = ufs_utp_query_retry(ufs, ATTR_R_DEVICEFFUSTATUS, 0);
		if (res)
			return res;
		status = ufs->attributes.arry[UPIU_ATTR_ID_DEVICEFFUSTATUS];
		if (status != FFU_STAT_SUCCESS)
			ufs_err("FFU error: 0x%x\n", status);
	}

	return res;
}

int ufs_sec_scan(void)
{
	int res = 0;

	res = scsi_scan(ufs_dev[0], 0, ufs_number_of_lus, scsi_exec, NULL, 128);
	if (res)
		printf("UFS scsi scan fail: %d\n", res);

	return res;
}

u16 get_ufs_man_id(void)
{
	struct ufs_host *ufs = get_cur_ufs_host();

	return ufs->wManufactureID;
}

u16 get_ufs_spec_ver(void)
{
	struct ufs_host *ufs = get_cur_ufs_host();

	return be16_to_cpu(ufs->device_desc.wSpecVersion);
}

void *get_ufs_pscm(void)
{
	return (void *)ptempscm;
}

scsi_device_t *get_scsi_dev(void)
{
	return ufs_dev[_ufs_curr_host];
}


