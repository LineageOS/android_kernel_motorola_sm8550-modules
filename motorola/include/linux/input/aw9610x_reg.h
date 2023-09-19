#ifndef _AW9610X_REG_H_
#define _AW9610X_REG_H_

/********************************************
* Register List
********************************************/
#define REG_SCANCTRL0			0x0000
#define REG_SCANCTRL1			0x0004
#define REG_SCANCTRL2			0x0008
#define REG_SCANCTRL3			0x000C
#define REG_AFECFG0_CH0			0x0010
#define REG_AFECFG1_CH0			0x0014
#define REG_AFECFG2_CH0			0x0018
#define REG_AFECFG3_CH0			0x001C
#define REG_AFECFG4_CH0			0x0020
#define REG_AFECFG0_CH1			0x0024
#define REG_AFECFG1_CH1			0x0028
#define REG_AFECFG2_CH1			0x002C
#define REG_AFECFG3_CH1			0x0030
#define REG_AFECFG4_CH1			0x0034
#define REG_AFECFG0_CH2			0x0038
#define REG_AFECFG1_CH2			0x003C
#define REG_AFECFG2_CH2			0x0040
#define REG_AFECFG3_CH2			0x0044
#define REG_AFECFG4_CH2			0x0048
#define REG_AFECFG0_CH3			0x004C
#define REG_AFECFG1_CH3			0x0050
#define REG_AFECFG2_CH3			0x0054
#define REG_AFECFG3_CH3			0x0058
#define REG_AFECFG4_CH3			0x005C
#define REG_AFECFG0_CH4			0x0060
#define REG_AFECFG1_CH4			0x0064
#define REG_AFECFG2_CH4			0x0068
#define REG_AFECFG3_CH4			0x006C
#define REG_AFECFG4_CH4			0x0070
#define REG_AFECFG0_CH5			0x0074
#define REG_AFECFG1_CH5			0x0078
#define REG_AFECFG2_CH5			0x007C
#define REG_AFECFG3_CH5			0x0080
#define REG_AFECFG4_CH5			0x0084
#define REG_FIRMVERSION			0x0088
#define REG_STAT0			0x0090
#define REG_STAT1			0x0094
#define REG_STAT2			0x0098
#define REG_PROXINTEN			0x009C
#define REG_FILTREF0_CH0		0x00A0
#define REG_FILTREF1_CH0		0x00A4
#define REG_BLFILT1_CH0			0x00A8
#define REG_BLFILT2_CH0			0x00AC
#define REG_PROXCTRL_CH0		0x00B0
#define REG_PROXTH0_CH0			0x00B8
#define REG_PROXTH1_CH0			0x00BC
#define REG_PROXTH2_CH0			0x00C0
#define REG_PROXTH3_CH0			0x00C4
#define REG_STDDET_CH0			0x00C8
#define REG_INITPROX0_CH0		0x00CC
#define REG_INITPROX1_CH0		0x00D0
#define REG_DATAOFFSET_CH0		0x00D4
#define REG_CORRTARDATA_CH0		0x00D8
#define REG_FILTREF0_CH1		0x00DC
#define REG_FILTREF1_CH1		0x00E0
#define REG_BLFILT1_CH1			0x00E4
#define REG_BLFILT2_CH1			0x00E8
#define REG_PROXCTRL_CH1		0x00EC
#define REG_PROXTH0_CH1			0x00F4
#define REG_PROXTH1_CH1			0x00F8
#define REG_PROXTH2_CH1			0x00FC
#define REG_PROXTH3_CH1			0x0100
#define REG_STDDET_CH1			0x0104
#define REG_INITPROX0_CH1		0x0108
#define REG_INITPROX1_CH1		0x010C
#define REG_DATAOFFSET_CH1		0x0110
#define REG_CORRTARDATA_CH1		0x0114
#define REG_FILTREF0_CH2		0x0118
#define REG_FILTREF1_CH2		0x011C
#define REG_BLFILT1_CH2			0x0120
#define REG_BLFILT2_CH2			0x0124
#define REG_PROXCTRL_CH2		0x0128
#define REG_PROXTH0_CH2			0x0130
#define REG_PROXTH1_CH2			0x0134
#define REG_PROXTH2_CH2			0x0138
#define REG_PROXTH3_CH2			0x013C
#define REG_STDDET_CH2			0x0140
#define REG_INITPROX0_CH2		0x0144
#define REG_INITPROX1_CH2		0x0148
#define REG_DATAOFFSET_CH2		0x014C
#define REG_BASELINE_CH2		0x0150
#define REG_FILTREF0_CH3		0x0154
#define REG_FILTREF1_CH3		0x0158
#define REG_BLFILT1_CH3			0x015C
#define REG_BLFILT2_CH3			0x0160
#define REG_PROXCTRL_CH3		0x0164
#define REG_PROXTH0_CH3			0x016C
#define REG_PROXTH1_CH3			0x0170
#define REG_PROXTH2_CH3			0x0174
#define REG_PROXTH3_CH3			0x0178
#define REG_STDDET_CH3			0x017C
#define REG_INITPROX0_CH3		0x0180
#define REG_INITPROX1_CH3		0x0184
#define REG_DATAOFFSET_CH3		0x0188
#define REG_CORRTARDATA_CH3		0x018C
#define REG_FILTREF0_CH4		0x0190
#define REG_FILTREF1_CH4		0x0194
#define REG_BLFILT1_CH4			0x0198
#define REG_BLFILT2_CH4			0x019C
#define REG_PROXCTRL_CH4		0x01A0
#define REG_PROXTH0_CH4			0x01A8
#define REG_PROXTH1_CH4			0x01AC
#define REG_PROXTH2_CH4			0x01B0
#define REG_PROXTH3_CH4			0x01B4
#define REG_STDDET_CH4			0x01B8
#define REG_INITPROX0_CH4		0x01BC
#define REG_INITPROX1_CH4		0x01C0
#define REG_DATAOFFSET_CH4		0x01C4
#define REG_CORRTARDATA_CH4		0x01C8
#define REG_FILTREF0_CH5		0x01CC
#define REG_FILTREF1_CH5		0x01D0
#define REG_BLFILT1_CH5			0x01D4
#define REG_BLFILT2_CH5			0x01D8
#define REG_PROXCTRL_CH5		0x01DC
#define REG_PROXTH0_CH5			0x01E4
#define REG_PROXTH1_CH5			0x01E8
#define REG_PROXTH2_CH5			0x01EC
#define REG_PROXTH3_CH5			0x01F0
#define REG_STDDET_CH5			0x01F4
#define REG_INITPROX0_CH5		0x01F8
#define REG_INITPROX1_CH5		0x01FC
#define REG_DATAOFFSET_CH5		0x0200
#define REG_CORRTARDATA_CH5		0x0204
#define REG_REFACFG			0x0208
#define REG_REFBCFG			0x020C
#define REG_VALID_CH0			0x0210
#define REG_VALID_CH1			0x0214
#define REG_VALID_CH2			0x0218
#define REG_VALID_CH3			0x021C
#define REG_VALID_CH4			0x0220
#define REG_VALID_CH5			0x0224
#define REG_BASELINE_CH0		0x0228
#define REG_DIFF_CH0			0x0240
#define REG_AMID_CH0			0x0258
#define REG_AMAD_CH0			0x0270
#define REG_AAMD_CH0			0x0288
#define REG_SMID_CH0			0x02A0
#define REG_SMAD_CH0			0x02B8
#define REG_RAW_CH0			0x02D0
#define REG_LOPT_CH0			0x02E8
#define REG_VAPD_CH0			0x0300
#define REG_VADD_CH0			0x0318
#define REG_PSCBD_CH0			0x0330
#define REG_AFD_CH0			0x0348
#define REG_EEDA0			0x0408
#define REG_EEDA1			0x040C
#define REG_DARE			0x41FC
#define REG_DICR			0x4400
#define REG_ITRE			0x4410
#define REG_PUUC			0x4420
#define REG_PUDN			0x4430
#define REG_IEB				0x4440
#define REG_PSR0			0x4450
#define REG_PSR1			0x4460
#define REG_CONRG			0x4470
#define REG_CMD				0xF008
#define REG_HOSTIRQSRC			0xF080
#define REG_HOSTIRQEN			0xF084
#define REG_MCR				0xF800
#define REG_ACR				0xF804
#define REG_HOSTCTRL1			0xFF00
#define REG_HOSTCTRL2			0xFF0C
#define REG_CHIP_ID			0xFF10
#define REG_ATN				0xFFE0
#define REG_HAAPEREN			0xFFF4
/********************************************
 * Register Access
 *******************************************/
#define REG_NONE_ACCESS			0
#define REG_RD_ACCESS			(1 << 0)
#define REG_WR_ACCESS			(1 << 1)

/**************************************
 *   define platform data
 *
 **************************************/
struct aw_reg_data {
	unsigned int reg;
	unsigned int val;
	unsigned char rw;
};

static const struct aw_reg_data aw9610x_reg_access[] = {
	{.reg = REG_SCANCTRL0, .rw = REG_RD_ACCESS|REG_WR_ACCESS,},
	{.reg = REG_SCANCTRL1, .rw = REG_RD_ACCESS|REG_WR_ACCESS,},
	{.reg = REG_SCANCTRL2, .rw = REG_RD_ACCESS|REG_WR_ACCESS,},
	{.reg = REG_SCANCTRL3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG0_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG1_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG2_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG3_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG4_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG0_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG1_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG2_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG3_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG4_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG0_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG1_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG2_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG3_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG4_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG0_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG1_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG2_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG3_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG4_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG0_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG1_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG2_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG3_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG4_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG0_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG1_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG2_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG3_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_AFECFG4_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STAT0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STAT1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STAT2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXINTEN, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF0_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF1_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT1_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT2_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXCTRL_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH0_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH1_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH2_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH3_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STDDET_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX0_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX1_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_DATAOFFSET_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_CORRTARDATA_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF0_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF1_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT1_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT2_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXCTRL_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH0_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH1_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH2_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH3_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STDDET_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX0_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX1_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_DATAOFFSET_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_CORRTARDATA_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF0_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF1_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT1_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT2_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXCTRL_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH0_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH1_CH2, .rw =  REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH2_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH3_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STDDET_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX0_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX1_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_DATAOFFSET_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BASELINE_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF0_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF1_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT1_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT2_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXCTRL_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH0_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH1_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH2_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH3_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STDDET_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX0_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX1_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_DATAOFFSET_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_CORRTARDATA_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF0_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF1_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT1_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT2_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXCTRL_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH0_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH1_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH2_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH3_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STDDET_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX0_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX1_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_DATAOFFSET_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_CORRTARDATA_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF0_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_FILTREF1_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT1_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_BLFILT2_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXCTRL_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH0_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH1_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH2_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PROXTH3_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_STDDET_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX0_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_INITPROX1_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_DATAOFFSET_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_CORRTARDATA_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_REFACFG, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_REFBCFG, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_VALID_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_BASELINE_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_DIFF_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_AMID_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_AMAD_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_AAMD_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_SMID_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_SMAD_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_RAW_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_LOPT_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_VAPD_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_VADD_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_PSCBD_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_AFD_CH0, .rw = REG_RD_ACCESS,},
	{.reg = REG_EEDA0, .rw = REG_RD_ACCESS,},
	{.reg = REG_EEDA1, .rw = REG_RD_ACCESS,},
	{.reg = REG_DARE, .rw = REG_RD_ACCESS,},
	{.reg = REG_DICR, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_ITRE, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PUUC, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PUDN, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_IEB, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PSR0, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_PSR1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_CONRG, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_HOSTIRQSRC, .rw = REG_RD_ACCESS,},
	{.reg = REG_HOSTIRQEN, .rw = REG_WR_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_MCR, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_ACR, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_HOSTCTRL1, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_HOSTCTRL2, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_CHIP_ID, .rw = REG_RD_ACCESS,},
	{.reg = REG_ATN, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
	{.reg = REG_HAAPEREN, .rw = REG_RD_ACCESS | REG_WR_ACCESS,},
};

#endif
