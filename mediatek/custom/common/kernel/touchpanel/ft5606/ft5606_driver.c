#include "tpd.h"
#include <linux/interrupt.h>
#include <cust_eint.h>
#include <linux/i2c.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/rtpm_prio.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include "tpd_custom_ft5606.h"

#include <mach/mt_pm_ldo.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_boot.h>

#include <cust_vibrator.h>
#include "cust_gpio_usage.h"


#define FTS_GESTRUE			//双击唤醒
#define FTS_PRESSURE		//压力和面积


#ifdef FTS_GESTRUE
#define GESTURE_LEFT		0x20
#define GESTURE_RIGHT		0x21
#define GESTURE_UP		    0x22
#define GESTURE_DOWN		0x23
#define GESTURE_DOUBLECLICK	0x24
#define GESTURE_O		    0x30
#define GESTURE_W		    0x31
#define GESTURE_M		    0x32
#define GESTURE_E		    0x33
#define GESTURE_C		    0x34
#define GESTURE_S		    0x46
#define GESTURE_V		    0x54
#define GESTURE_Z		    0x41

#define FTS_GESTRUE_POINTS 255
#define FTS_GESTRUE_POINTS_ONETIME  62
#define FTS_GESTRUE_POINTS_HEADER 8
#define FTS_GESTURE_OUTPUT_ADRESS 0xD3
#define FTS_GESTURE_OUTPUT_UNIT_LENGTH 4

//suspend_state_t get_suspend_state(void);

unsigned short coordinate_x[150] = {0};
unsigned short coordinate_y[150] = {0};
//是否支持双击唤醒功能
unsigned char GestrueEnable=1; //0-不支持 1-支持
#endif

#ifdef FTS_PRESSURE
//#define FT_TOUCH_WEIGHT         7  //ERIC ADD
//#define FT_TOUCH_AREA           8//ERIC ADD

#define PRESS_MAX	0xFF
#define FT_PRESS	0x08
#endif

#define FTS_CTL_IIC

#ifdef FTS_CTL_IIC
#include "focaltech_ctl.h"
#endif


//lenovo_sw liaohj merged from putaoya 2012-09-12
#define TPD_MAX_PONIT       10//5 

extern void custom_vibration_enable(int);

#define uart_log
 enum wk_wdt_type {
	WK_WDT_LOC_TYPE,
	WK_WDT_EXT_TYPE,
	WK_WDT_LOC_TYPE_NOLOCK,
	WK_WDT_EXT_TYPE_NOLOCK,
	
};
extern void mtk_wdt_restart(enum wk_wdt_type type);
 
extern struct tpd_device *tpd;
 
struct i2c_client *i2c_client = NULL;
struct task_struct *thread = NULL;
 
static DECLARE_WAIT_QUEUE_HEAD(waiter);
 
static int autocali_status = 0; //wuboadd
static int autocali_status_step1 = 0; //wuboadd
static int autocali_status_step2 = 0; //wuboadd
static int cali_83_status_check = 0; //robin
static int cali_84_status_check = 0; //robin

static void tpd_eint_interrupt_handler(void);
 #if 0
extern void mt65xx_eint_unmask(unsigned int line);
extern void mt65xx_eint_mask(unsigned int line);
extern void mt65xx_eint_set_hw_debounce(unsigned int eint_num, unsigned int ms);
extern unsigned int mt65xx_eint_set_sens(unsigned int eint_num, unsigned int sens);
extern void mt65xx_eint_registration(unsigned int eint_num, unsigned int is_deb_en, unsigned int pol, void (EINT_FUNC_PTR)(void), unsigned int is_auto_umask);
#endif
 
static int __devinit tpd_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tpd_detect (struct i2c_client *client, struct i2c_board_info *info);
static int __devexit tpd_remove(struct i2c_client *client);
static int touch_event_handler(void *unused);
static struct proc_dir_entry *ft5316_config_proc = NULL;
static int boot_mode = 0;
static int tpd_halt=0; 
static int tpd_flag = 0;
static int point_num = 0;
static int p_point_num = 0;
static bool discard_resume_first_eint = KAL_FALSE;
static int tpd_state = 0;
//#define TPD_CLOSE_POWER_IN_SLEEP

#define TPD_OK 0
//register define

#define DEVICE_MODE 0x00
#define GEST_ID 0x01
#define TD_STATUS 0x02

#define TOUCH1_XH 0x03
#define TOUCH1_XL 0x04
#define TOUCH1_YH 0x05
#define TOUCH1_YL 0x06

#define TOUCH2_XH 0x09
#define TOUCH2_XL 0x0A
#define TOUCH2_YH 0x0B
#define TOUCH2_YL 0x0C

#define TOUCH3_XH 0x0F
#define TOUCH3_XL 0x10
#define TOUCH3_YH 0x11
#define TOUCH3_YL 0x12

/*add for portugal evt*/
/* Lenovo-sw yexm1 modify, 2012-10-18, open the FW upgrade fun */
#define CONFIG_SUPPORT_FTS_CTP_UPG

//#define ESD_CHECK

#define TPD_RESET_ISSUE_WORKAROUND

#define TPD_MAX_RESET_COUNT 3


#ifdef ESD_CHECK
static struct delayed_work ctp_read_id_work;
static struct workqueue_struct * ctp_read_id_workqueue = NULL;
#endif

#ifdef TPD_HAVE_BUTTON 
static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif
#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
static int tpd_wb_start_local[TPD_WARP_CNT] = TPD_WARP_START;
static int tpd_wb_end_local[TPD_WARP_CNT]   = TPD_WARP_END;
#endif
#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
static int tpd_calmat_local[8]     = TPD_CALIBRATION_MATRIX;
static int tpd_def_calmat_local[8] = TPD_CALIBRATION_MATRIX;
#endif

#define VELOCITY_CUSTOM_FT5606
#ifdef VELOCITY_CUSTOM_FT5606
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

// for magnify velocity********************************************

#ifndef TPD_VELOCITY_CUSTOM_X
#define TPD_VELOCITY_CUSTOM_X 10
#endif
#ifndef TPD_VELOCITY_CUSTOM_Y
#define TPD_VELOCITY_CUSTOM_Y 10
#endif

#define TOUCH_IOC_MAGIC 'A'

#define TPD_GET_VELOCITY_CUSTOM_X _IO(TOUCH_IOC_MAGIC,0)
#define TPD_GET_VELOCITY_CUSTOM_Y _IO(TOUCH_IOC_MAGIC,1)
#define TPD_GET_ENABLE_GESTRUE _IO(TOUCH_IOC_MAGIC,3)
#define TPD_SET_ENABLE_GESTRUE _IO(TOUCH_IOC_MAGIC,4)


#if defined (CONFIG_SUPPORT_FTS_CTP_UPG)  // 苏 勇 2013年11月19日 13:39:55
#define TPD_UPGRADE_CKT _IO(TOUCH_IOC_MAGIC,2)
static unsigned char CtpFwUpgradeForIOCTRL(unsigned char* pbt_buf, unsigned int dw_lenth);
static DEFINE_MUTEX(fwupgrade_mutex);
atomic_t    upgrading;
#endif /* CONFIG_SUPPORT_FTS_CTP_UPG */


int g_v_magnify_x =TPD_VELOCITY_CUSTOM_X;
int g_v_magnify_y =TPD_VELOCITY_CUSTOM_Y;
static int tpd_misc_open(struct inode *inode, struct file *file)
{
/*
	file->private_data = adxl345_i2c_client;

	if(file->private_data == NULL)
	{
		printk("tpd: null pointer!!\n");
		return -EINVAL;
	}
	*/
	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int tpd_misc_release(struct inode *inode, struct file *file)
{
	//file->private_data = NULL;
	return 0;
}
/*----------------------------------------------------------------------------*/
//static int adxl345_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
//       unsigned long arg)
static long tpd_unlocked_ioctl(struct file *file, unsigned int cmd,
       unsigned long arg)
{
	//struct i2c_client *client = (struct i2c_client*)file->private_data;
	//struct adxl345_i2c_data *obj = (struct adxl345_i2c_data*)i2c_get_clientdata(client);	
	//char strbuf[256];
	void __user *data;
	long err = 0;
	int size=0;
	char * ctpdata=NULL;
	
	if(_IOC_DIR(cmd) & _IOC_READ)
	{
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	}
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
	{
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}

	if(err)
	{
		printk("tpd: access error: %08X, (%2d, %2d)\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch(cmd)
	{
		case TPD_GET_VELOCITY_CUSTOM_X:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}			
			
			if(copy_to_user(data, &g_v_magnify_x, sizeof(g_v_magnify_x)))
			{
				err = -EFAULT;
				break;
			}				 
			break;

	   case TPD_GET_VELOCITY_CUSTOM_Y:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}			
			
			if(copy_to_user(data, &g_v_magnify_y, sizeof(g_v_magnify_y)))
			{
				err = -EFAULT;
				break;
			}				 
			break;
			
		#ifdef FTS_GESTRUE
		case TPD_GET_ENABLE_GESTRUE:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}			
			
			if(copy_to_user(data, &GestrueEnable, sizeof(GestrueEnable)))
			{
				err = -EFAULT;
				break;
			}	
			break;

		case TPD_SET_ENABLE_GESTRUE:
			data = (void __user *) arg;
			
			if(copy_from_user(GestrueEnable, &data, sizeof(GestrueEnable)))
			{
				err = -EFAULT;
				break;
			}		
			break;
		#endif

#if defined (CONFIG_SUPPORT_FTS_CTP_UPG)  // 苏 勇 2013年11月19日 13:39:46
		case TPD_UPGRADE_CKT:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}	
			if(copy_from_user(&size, data, sizeof(int)))
			{
				err = -EFAULT;
				break;	  
			}
			ctpdata=kmalloc(size, GFP_KERNEL);
			if(ctpdata==NULL)
			{
				err = -EFAULT;
				break;
			}
			
			if(copy_from_user(ctpdata, data+sizeof(int), size))
			{
				kfree(ctpdata);
				err = -EFAULT;
				break;	  
			}
			err=CtpFwUpgradeForIOCTRL(ctpdata, size);
 			kfree(ctpdata);
			break;
#endif /* CONFIG_SUPPORT_FTS_CTP_UPG */

		default:
			printk("tpd: unknown IOCTL: 0x%08x\n", cmd);
			err = -ENOIOCTLCMD;
			break;
			
	}

	return err;
}


static struct file_operations tpd_fops = {
//	.owner = THIS_MODULE,
	.open = tpd_misc_open,
	.release = tpd_misc_release,
	.unlocked_ioctl = tpd_unlocked_ioctl,
};
/*----------------------------------------------------------------------------*/
static struct miscdevice tpd_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ft5606",
	.fops = &tpd_fops,
};

//**********************************************
#endif

struct touch_info {
    int y[10];
    int x[10];
    int p[10];
	int id[10];
    int count;
    #ifdef FTS_PRESSURE
    int au8_touch_weight[TPD_MAX_PONIT];/*touch weight*/
    int au8_touch_area[TPD_MAX_PONIT];/*touch area*/
    #endif
};
 
static const struct i2c_device_id ft5606_tpd_id[] = {{"ft5606",0},{}};
//unsigned short force[] = {0,0x70,I2C_CLIENT_END,I2C_CLIENT_END}; 
//static const unsigned short * const forces[] = { force, NULL };
//static struct i2c_client_address_data addr_data = { .forces = forces, };
static struct i2c_board_info __initdata ft5606_i2c_tpd={ I2C_BOARD_INFO("ft5606", (0x70>>1))};
 
 
static struct i2c_driver tpd_i2c_driver = {
	.driver = {
		.name = "ft5606",//.name = TPD_DEVICE,
		//.owner = THIS_MODULE,
	},
	.probe = tpd_probe,
	.remove = __devexit_p(tpd_remove),
	.id_table = ft5606_tpd_id,
	.detect = tpd_detect,
	//.address_data = &addr_data,
};
#ifdef CONFIG_SUPPORT_FTS_CTP_UPG
static u8 *CTPI2CDMABuf_va = NULL;
static u32 CTPI2CDMABuf_pa = NULL;
typedef enum
{
    ERR_OK,
    ERR_MODE,
    ERR_READID,
    ERR_ERASE,
    ERR_STATUS,
    ERR_ECC,
    ERR_DL_ERASE_FAIL,
    ERR_DL_PROGRAM_FAIL,
    ERR_DL_VERIFY_FAIL
}E_UPGRADE_ERR_TYPE;

typedef unsigned char         FTS_BYTE;     //8 bit
typedef unsigned short        FTS_WORD;    //16 bit
typedef unsigned int          FTS_DWRD;    //16 bit
typedef unsigned char         FTS_BOOL;    //8 bit

#define FTS_NULL                0x0
#define FTS_TRUE                0x01
#define FTS_FALSE              0x0

#define I2C_CTPM_ADDRESS       0x70
#define FTS_UPGRADE_LOOP    5
/***********************************************************************************************
Name	:	ft5x0x_i2c_rxdata 

Input	:	*rxdata
                     *length

Output	:	ret

function	:	

***********************************************************************************************/
static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= i2c_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		},
		{
			.addr	= i2c_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

	//msleep(1);
	ret = i2c_transfer(i2c_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
	
	return ret;
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int ft5x0x_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= i2c_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

   	//msleep(1);
	ret = i2c_transfer(i2c_client->adapter, msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}
/***********************************************************************************************
Name	:	 ft5x0x_write_reg

Input	:	addr -- address
                     para -- parameter

Output	:	

function	:	write register of ft5x0x

***********************************************************************************************/
static int ft5x0x_write_reg(u8 addr, u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = addr;
    buf[1] = para;
    ret = ft5x0x_i2c_txdata(buf, 2);
    if (ret < 0) {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }
    
    return 0;
}


/***********************************************************************************************
Name	:	ft5x0x_read_reg 

Input	:	addr
                     pdata

Output	:	

function	:	read register of ft5x0x

***********************************************************************************************/
static int ft5x0x_read_reg(u8 addr, u8 *pdata)
{
	int ret;
	u8 buf[2] = {0};

	buf[0] = addr;
	struct i2c_msg msgs[] = {
		{
			.addr	= i2c_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= i2c_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= buf,
		},
	};

	//msleep(1);
	ret = i2c_transfer(i2c_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

	*pdata = buf[0];
	return ret;
  
}


/***********************************************************************************************
Name	:	 ft5x0x_read_fw_ver

Input	:	 void
                     

Output	:	 firmware version 	

function	:	 read TP firmware version

***********************************************************************************************/
static unsigned char ft5x0x_read_fw_ver(void)
{
	unsigned char ver;
	ft5x0x_read_reg(0xa6, &ver);
	return(ver);
}
static unsigned char ft5x0x_read_ID_ver(void)
{
	unsigned char ver;
	ft5x0x_read_reg(0xa8, &ver);
	return(ver);
}

static unsigned char ft5x0x_read_doubleclick_flag(void)
{
	unsigned char ver;
	ft5x0x_read_reg(0xcc, &ver);
	return(ver==0xaa?1:0);
}

void delay_qt_ms(unsigned long  w_ms)
{
	unsigned long i;
	unsigned long j;

	for (i = 0; i < w_ms; i++)
	{
		for (j = 0; j < 1000; j++)
		{
		    udelay(1);
		}
	}
}

/*
[function]: 
    callback: read data from ctpm by i2c interface,implemented by special user;
[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[out]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;
[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/
FTS_BOOL i2c_read_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
	int ret;

	ret=i2c_master_recv(i2c_client, pbt_buf, dw_lenth);

	if(ret<=0)
	{
		printk("[TSP]i2c_read_interface error\n");
		return FTS_FALSE;
	}

	return FTS_TRUE;
}

/*
[function]: 
    callback: write data to ctpm by i2c interface,implemented by special user;
[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[in]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;
[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/
FTS_BOOL i2c_write_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    int ret;
    ret=i2c_master_send(i2c_client, pbt_buf, dw_lenth);
    if(ret<=0)
    {
        printk("[TSP]i2c_write_interface error line = %d, ret = %d\n", __LINE__, ret);
        return FTS_FALSE;
    }

    return FTS_TRUE;
}

/*
[function]: 
    send a command to ctpm.
[parameters]:
    btcmd[in]        :command code;
    btPara1[in]    :parameter 1;    
    btPara2[in]    :parameter 2;    
    btPara3[in]    :parameter 3;    
    num[in]        :the valid input parameter numbers, if only command code needed and no parameters followed,then the num is 1;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL cmd_write(FTS_BYTE btcmd,FTS_BYTE btPara1,FTS_BYTE btPara2,FTS_BYTE btPara3,FTS_BYTE num)
{
    FTS_BYTE write_cmd[4] = {0};

    write_cmd[0] = btcmd;
    write_cmd[1] = btPara1;
    write_cmd[2] = btPara2;
    write_cmd[3] = btPara3;
    return i2c_write_interface(I2C_CTPM_ADDRESS, write_cmd, num);
}

/*
[function]: 
    write data to ctpm , the destination address is 0.
[parameters]:
    pbt_buf[in]    :point to data buffer;
    bt_len[in]        :the data numbers;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL byte_write(FTS_BYTE* pbt_buf, FTS_DWRD dw_len)
{
    
    return i2c_write_interface(I2C_CTPM_ADDRESS, pbt_buf, dw_len);
}


static int CTPDMA_i2c_write(FTS_BYTE slave,FTS_BYTE* pbt_buf, FTS_DWRD dw_len)
{
    
	int i = 0;
	int err = 0;
	for(i = 0 ; i < dw_len; i++)
	{
		CTPI2CDMABuf_va[i] = pbt_buf[i];
	}

	if(dw_len <= 8)
	{
		//i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG;
		//MSE_ERR("Sensor non-dma write timing is %x!\r\n", this_client->timing);
		return i2c_master_send(i2c_client, pbt_buf, dw_len);
	}
	else
	{
		i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG | I2C_DMA_FLAG;
		//MSE_ERR("Sensor dma timing is %x!\r\n", this_client->timing);
		err= i2c_master_send(i2c_client, CTPI2CDMABuf_pa, dw_len);
		i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG;
		return err;
	}    
}


static int CTPDMA_i2c_read(FTS_BYTE slave, FTS_BYTE *buf, FTS_DWRD len)
{
	int i = 0, err = 0;

	if(len < 8)
	{
		i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG;
		//MSE_ERR("Sensor non-dma read timing is %x!\r\n", this_client->timing);
		return i2c_master_recv(i2c_client, buf, len);
	}
	else
	{
		i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG | I2C_DMA_FLAG;
		//MSE_ERR("Sensor dma read timing is %x!\r\n", this_client->timing);
		err = i2c_master_recv(i2c_client, CTPI2CDMABuf_pa, len);
		i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG;
		if(err < 0)
		{
			return err;
		}

		for(i = 0; i < len; i++)
		{
			buf[i] = CTPI2CDMABuf_va[i];
		}
		return err;
	}
}


/*
[function]: 
    read out data from ctpm,the destination address is 0.
[parameters]:
    pbt_buf[out]    :point to data buffer;
    bt_len[in]        :the data numbers;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL byte_read(FTS_BYTE* pbt_buf, FTS_BYTE bt_len)
{
    return i2c_read_interface(I2C_CTPM_ADDRESS, pbt_buf, bt_len);
}


/*
[function]: 
    burn the FW to ctpm.
[parameters]:(ref. SPEC)
    pbt_buf[in]    :point to Head+FW ;
    dw_lenth[in]:the length of the FW + 6(the Head length);    
    bt_ecc[in]    :the ECC of the FW
[return]:
    ERR_OK        :no error;
    ERR_MODE    :fail to switch to UPDATE mode;
    ERR_READID    :read id fail;
    ERR_ERASE    :erase chip fail;
    ERR_STATUS    :status error;
    ERR_ECC        :ecc error.
*/


#define    FTS_PACKET_LENGTH        128

static unsigned char CTPM_FW[]=
{
    #include "BQ_KaiTo_CKT_FT5606_Truly0x5a_Ver0x17_20141111_app.i"
};


#define IC_FT5X06	0
#define IC_FT5606	1
#define IC_FT5316	2
#define IC_FT5X36	3
#define DEVICE_IC_TYPE	IC_FT5X36

#define BL_VERSION_LZ4        0
#define BL_VERSION_Z7        1
#define BL_VERSION_GZF        2


int fts_ctpm_auto_clb()
{
	unsigned char uc_temp;
	unsigned char i ;

	/*start auto CLB*/
	msleep(200);
	ft5x0x_write_reg(0, 0x40);
	msleep(100);   /*make sure already enter factory mode*/
	ft5x0x_write_reg(2, 0x4);  /*write command to start calibration*/
	msleep(300);
	if (DEVICE_IC_TYPE == IC_FT5X36) {
		for(i=0;i<100;i++)
		{
			ft5x0x_read_reg(0x02, &uc_temp);
			if (0x02 == uc_temp ||
				0xFF == uc_temp)
			{
				/*if 0x02, then auto clb ok, else 0xff, auto clb failure*/
			    break;
			}
			msleep(20);
		}
	} else {
		for(i=0;i<100;i++)
		{
			ft5x0x_read_reg(0, &uc_temp);
			if (0x0 == ((uc_temp&0x70)>>4))  /*return to normal mode, calibration finish*/
			{
			    autocali_status = 0xCA;
			    printk("wuboadd calibration success uc_temp = %x\n",uc_temp);
			    break;
			}
			msleep(20);
		}
	}
	/*calibration OK*/
	ft5x0x_write_reg(0, 0x40);  /*goto factory mode for store*/
	msleep(200);   /*make sure already enter factory mode*/
	ft5x0x_write_reg(2, 0x5);  /*store CLB result*/
	msleep(300);
	ft5x0x_write_reg(0, 0x0); /*return to normal mode*/
	msleep(300);
	/*store CLB result OK*/

	return 0;
}


E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
    FTS_BYTE reg_val[2] = {0};
    FTS_DWRD i = 0;

    FTS_DWRD  packet_number;
    FTS_DWRD  j;
    FTS_DWRD  temp;
    FTS_DWRD  lenght;
    FTS_BYTE  packet_buf[FTS_PACKET_LENGTH + 6];
    FTS_BYTE  auc_i2c_write_buf[10];
    FTS_BYTE  bt_ecc;
    int i_ret;
    int ret;
    unsigned char ver;
    /*********Step 1:Reset  CTPM *****/
	atomic_set(&upgrading, 1);
	mutex_lock(&fwupgrade_mutex);
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);    
#if 0
    /*write 0xaa to register 0xfc*/

    mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
    mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
    msleep(10);  
    mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
    mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
    //printk("[TSP] Step 1: Reset CTPM test\n");
   
    delay_qt_ms(50);   	
#else
    /*write 0xaa to register 0xfc*/
    ft5x0x_write_reg(0xfc,0xaa);
    delay_qt_ms(50);
    /*write 0x55 to register 0xfc*/
    ft5x0x_write_reg(0xfc,0x55);
    printk("[FTS] Step 1: Reset CTPM test\n");
   
    delay_qt_ms(50);  
#endif

    /*********Step 2:Enter upgrade mode *****/
    auc_i2c_write_buf[0] = 0x55;
    auc_i2c_write_buf[1] = 0xaa;
    do
    {
        i ++;
        i_ret = ft5x0x_i2c_txdata(auc_i2c_write_buf, 2);
        delay_qt_ms(5);
    }while(i_ret <= 0 && i < 5 );

    /*********Step 3:check READ-ID***********************/        
    cmd_write(0x90,0x00,0x00,0x00,4);
    i=0;
    i_ret=0;
    do
    {
        i ++;
        i_ret = byte_read(reg_val,2);
        delay_qt_ms(20);//(10);
    }while(i_ret <= 0 && i < 5 );
    printk("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x,i_ret=%d,i=%d\n",reg_val[0],reg_val[1],i_ret,i);
    #if 0
    if (reg_val[0] == 0x79 && reg_val[1] == 0x3)
    {
        printk("[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
    }
    else
    {
        return ERR_READID;
        //i_is_new_protocol = 1;
    }
    #endif
    auc_i2c_write_buf[0] = 0xcd;
    cmd_write(auc_i2c_write_buf[0],0x0,0x00,0x00,1);
    byte_read(reg_val,1);
    printk("[FTS] bootloader version = 0x%x\n", reg_val[0]);

    /*********Step 4:erase app and panel paramenter area ********************/
    cmd_write(0x61,0x00,0x00,0x00,1);  //erase app area
    delay_qt_ms(2000);//(1500); 
    cmd_write(0x63,0x00,0x00,0x00,1);  //erase panel parameter area
    delay_qt_ms(150);//(100);
    printk("[FTS] Step 4: erase. \n");

    /*********Step 5:write firmware(FW) to ctpm flash*********/
    bt_ecc = 0;
    printk("[FTS] Step 5: start upgrade. \n");
    dw_lenth = dw_lenth - 8;
    packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;
    for (j=0;j<packet_number;j++)
    {
        temp = j * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        lenght = FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(lenght>>8);
        packet_buf[5] = (FTS_BYTE)lenght;

        for (i=0;i<FTS_PACKET_LENGTH;i++)
        {
            packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }
        
        //byte_write(&packet_buf[0],FTS_PACKET_LENGTH + 6);
        CTPDMA_i2c_write(0x70, &packet_buf[0],FTS_PACKET_LENGTH + 6);
        delay_qt_ms(30);//(FTS_PACKET_LENGTH/6 + 1);
        if ((j * FTS_PACKET_LENGTH % 1024) == 0)
        {
              printk("[FTS] upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
        }
    }

    if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
    {
        temp = packet_number * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;

        temp = (dw_lenth) % FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;

        for (i=0;i<temp;i++)
        {
            packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }

        //byte_write(&packet_buf[0],temp+6);    
        CTPDMA_i2c_write(0x70, &packet_buf[0],temp+6);
        delay_qt_ms(30);//(20);
    }

    //send the last six byte
    for (i = 0; i<6; i++)
    {
        temp = 0x6ffa + i;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        temp =1;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;
        packet_buf[6] = pbt_buf[ dw_lenth + i]; 
        bt_ecc ^= packet_buf[6];
        
        printk("======== packet_buf[2~5] = 0x%x 0x%x 0x%x 0x%x ========\n",packet_buf[2],packet_buf[3],packet_buf[4],packet_buf[5]);
        printk("======== packet_buf[6] = 0x%x bt_ecc = 0x%x ========\n",packet_buf[6],bt_ecc);

        //byte_write(&packet_buf[0],7);  
        CTPDMA_i2c_write(0x70, &packet_buf[0],7);
        delay_qt_ms(10);//(20);
    }

    /*********Step 6: read out checksum***********************/
    /*send the opration head*/
#if 1
    i2c_smbus_read_i2c_block_data(i2c_client, 0xcc, 1, &(reg_val[0]));
#else
    cmd_write(0xcc,0x00,0x00,0x00,1);
    byte_read(reg_val,1);
#endif
    printk("[FTS] Step 6:  ecc read 0x%x, new firmware 0x%x. \n", reg_val[0], bt_ecc);
    if(reg_val[0] != bt_ecc)
    {
        //return ERR_ECC;
        ret = -1;
        goto ERR;
    }

    /*********Step 7: reset the new FW***********************/
#if 1//0
    mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
    mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
    msleep(1);  
    mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
    mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
    ret = 0;
    printk("==== <%d>,%s(),Reset the new FW ====\n",__LINE__,__func__ );
#else
    cmd_write(0x07,0x00,0x00,0x00,1);

    msleep(300);  //make sure CTP startup normally
#endif
ERR:
    mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
    mutex_unlock(&fwupgrade_mutex);
    atomic_set(&upgrading, 0);
    
    //return ERR_OK;
    printk("======== ret=%d ========\n");
    return ret;
}

static int fts_ctpm_fw_upgrade_with_i_file(void)
{
	FTS_BYTE*     pbt_buf = FTS_NULL;
	int i_ret;
	unsigned char version=0;
	FTS_BYTE flag;
	FTS_DWRD i = 0;
	//=========FW upgrade========================*/
	pbt_buf = CTPM_FW;
	
	printk("version=%x ,pbt_buf[sizeof(CTPM_FW)-2]=%x\n",version,pbt_buf[sizeof(CTPM_FW)-2]);
	printk("[TSP]ID_ver=%x, fw_ver=%x\n", ft5x0x_read_ID_ver(), ft5x0x_read_fw_ver());
#if 0 /*zhouwl, temp disable this line*/
if(0xa8 != ft5x0x_read_ID_ver())
{
	if(ft5x0x_read_ID_ver() != pbt_buf[sizeof(CTPM_FW)-1])
	{
        	return;
	}
	
	do
	{
		i ++;
		version =ft5x0x_read_fw_ver();
		delay_qt_ms(2);
	}while( i < 5 );
    
	if(version==pbt_buf[sizeof(CTPM_FW)-2])
	{
		return;
	}
}
#endif
	/*call the upgrade function*/
	i_ret =  fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW));
	mtk_wdt_restart(WK_WDT_EXT_TYPE);            //feed dog
	if (i_ret != 0)
	{
		autocali_status_step1 =0xFA;
		autocali_status_step2 = 0xFA;
		printk("[TSP]upgrade error\n");
		//error handling ...
		//TBD
	}
	else
	{
		autocali_status_step1 =0xEA;
		autocali_status_step2 = 0xEA;
		printk("wubo add auto calibration start\n");
		fts_ctpm_auto_clb();
		printk("wubo add auto calibration end\n");
	}
	msleep(200);  
	ft5x0x_write_reg(0xfc,0x04);
	msleep(4000);
	flag=0;
	i2c_smbus_read_i2c_block_data(i2c_client, 0xFC, 1, &flag);
	//printk("flag=%d\n",flag);
	return i_ret;
}

unsigned char fts_ctpm_get_upg_ver(void)
{
    unsigned int ui_sz;
    ui_sz = sizeof(CTPM_FW);
    if (ui_sz > 2)
    {
        return CTPM_FW[ui_sz - 2];
    }
    else
    {
        //TBD, error handling?
        return 0xff; //default value
    }
}


static void tpd_resume( struct early_suspend *h );
static unsigned char CtpFwUpgradeForIOCTRL(unsigned char* pbt_buf, unsigned int dw_lenth)
{
	int ret=0;
	
	tpd_resume((struct early_suspend *)0); // 升级的时候唤醒,为了简单直接调用唤醒函数 苏 勇 2014年01月14日 17:01:17
	ret=fts_ctpm_fw_upgrade(pbt_buf,dw_lenth);
    //wangli_20141013 add for calibration to avoid TP problem
	mtk_wdt_restart(WK_WDT_EXT_TYPE);
	if (ret != 0)
	{
		autocali_status_step1 =0xFA;
		autocali_status_step2 = 0xFA;
		printk("==== [TSP]upgrade error ====\n");
		//error handling ...
		//TBD
	}
	else
	{
		autocali_status_step1 =0xEA;
		autocali_status_step2 = 0xEA;
		printk("==== wangli add auto calibration start ====\n");
		fts_ctpm_auto_clb();
		printk("==== wangli add auto calibration end ====\n");
	}

	msleep(200);  
	ft5x0x_write_reg(0xfc,0x04);
	msleep(4000);
   	return ret;
}

#endif
#ifdef ESD_CHECK	
static void ESD_read_id_workqueue(struct work_struct *work)
{
	char data;
	if(tpd_halt) 
		return; 
	i2c_smbus_read_i2c_block_data(i2c_client, 0x88, 1, &data);
//	TPD_DEBUG("ESD_read_id_workqueue data: %d\n", data);
	printk("ESD_read_id_workqueue data: %d\n", data);
	if((data > 5)&&(data < 10))
	{
		//add_timer();
	}
	else
	{

	 	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
		if(tpd_state)
		{
			input_mt_sync(tpd->dev);
	                input_sync(tpd->dev);
			tpd_state = 0;
		}
		msleep(5);  
	
//#ifdef MT6575
		//power on, need confirm with SA
#ifdef TPD_POWER_SOURCE_CUSTOM
    		hwPowerDown(TPD_POWER_SOURCE_CUSTOM,  "TP");
#else
    		hwPowerDown(MT65XX_POWER_LDO_VGP2,  "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
    		hwPowerDown(TPD_POWER_SOURCE_1800,  "TP");
#endif    
		msleep(5);  
#ifdef TPD_POWER_SOURCE_CUSTOM
    		hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
#else
    		hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
    		hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif    
//#endif	
		msleep(100);
		mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
		mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
		msleep(10);  
		mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
		mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
	 	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); 
		 
		msleep(200);
	}
	if(tpd_halt) 
	{
		//#ifdef FTS_GESTRUE
		//if(GestrueEnable != 0)
		//#endif
		mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM); 
	}
	else 
		queue_delayed_work(ctp_read_id_workqueue, &ctp_read_id_work,400); //schedule a work for the first detection					

}
#endif
#ifdef FTS_PRESSURE
static  void tpd_down(int x, int y, int p, int w,int a) {
#else
static  void tpd_down(int x, int y, int p) {
#endif
	//input_report_abs(tpd->dev, ABS_PRESSURE, p);
	input_report_key(tpd->dev, BTN_TOUCH, 1);
	#ifdef FTS_PRESSURE
	input_report_abs(tpd->dev, ABS_MT_PRESSURE, w);
	input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, a);
	#else
	input_report_abs(tpd->dev, ABS_MT_PRESSURE, 1);
	input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 20);
	#endif
	input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
	input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
	/* Lenovo-sw yexm1, optimize the code, 2012-9-19 begin */
	// printk("D[%4d %4d %4d] ", x, y, p);
	/* Lenovo-sw yexm1, optimize the code, 2012-9-19 end */
	/* track id Start 0 */
	input_report_abs(tpd->dev, ABS_MT_TRACKING_ID, p); 
	input_mt_sync(tpd->dev);
	if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
	{   
		tpd_button(x, y, 1);  
	}
	if(y > TPD_RES_Y) //virtual key debounce to avoid android ANR issue
	{
        	/* Lenovo-sw yexm1 modify, 2012-10-15, delete the delay */
        	//msleep(50);
		printk("D virtual key \n");
	}
	TPD_EM_PRINT(x, y, x, y, p-1, 1);
 }
 
static  void tpd_up(int x, int y,int *count) {
	 //if(*count>0) {
	 	#ifdef FTS_PRESSURE
		input_report_abs(tpd->dev, ABS_MT_PRESSURE, 0);
		input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 0);
		#else
		input_report_abs(tpd->dev, ABS_MT_PRESSURE, 0);
		#endif
		input_report_key(tpd->dev, BTN_TOUCH, 0);
		//input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 0);
		//input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
		//input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
		//printk("U[%4d %4d %4d] ", x, y, 0);
		input_mt_sync(tpd->dev);
		TPD_EM_PRINT(x, y, x, y, 0, 0);
		//(*count)--;
	if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
	{   
		tpd_button(x, y, 0); 
	}   		 

 }

 static int tpd_touchinfo(struct touch_info *cinfo, struct touch_info *pinfo)
 {

	int i = 0;
	
	char data[(3+6*(TPD_MAX_PONIT-1)+3+1+7)/8*8] = {0};
	u8 version=0xff;

	u16 high_byte,low_byte;
	u8 report_rate =0;

	p_point_num = point_num;

#if 0
	/* Lenovo-sw yexm1, optimize the code, 2012-9-19 begin */
	i2c_smbus_read_i2c_block_data(i2c_client, 0x00, 32, &(data[0]));
	//i2c_smbus_read_i2c_block_data(i2c_client, 0x08, 8, &(data[8]));
	//i2c_smbus_read_i2c_block_data(i2c_client, 0x10, 8, &(data[16]));
	//i2c_smbus_read_i2c_block_data(i2c_client, 0x18, 8, &(data[24]));
	//i2c_smbus_read_i2c_block_data(i2c_client, 0xa6, 1, &(data[32]));
#else
	for (i<0;i<(3+6*(TPD_MAX_PONIT-1)+3+1+7)/8;i++)
	{
		i2c_smbus_read_i2c_block_data(i2c_client, 0x00+i*8, 8, &(data[i*8]));
	}
	i2c_smbus_read_i2c_block_data(i2c_client, 0xa6, 1, &version);
#endif

	/* Lenovo-sw yexm1, optimize the code, 2012-9-19 end */
	i2c_smbus_read_i2c_block_data(i2c_client, 0x88, 1, &report_rate);
	//TPD_DEBUG("received raw data from touch panel as following:\n");
	//TPD_DEBUG("[data[0]=%x,data[1]= %x ,data[2]=%x ,data[3]=%x ,data[4]=%x ,data[5]=%x]\n",data[0],data[1],data[2],data[3],data[4],data[5]);
	//TPD_DEBUG("[data[9]=%x,data[10]= %x ,data[11]=%x ,data[12]=%x]\n",data[9],data[10],data[11],data[12]);
	//TPD_DEBUG("[data[15]=%x,data[16]= %x ,data[17]=%x ,data[18]=%x]\n",data[15],data[16],data[17],data[18]);
    
	//we have  to re update report rate
	//TPD_DMESG("report rate =%x\n",report_rate);
	if(report_rate < 8)
	{
		report_rate = 0x8;
		if((i2c_smbus_write_i2c_block_data(i2c_client, 0x88, 1, &report_rate))< 0)
		{
			TPD_DMESG("I2C read report rate error, line: %d\n", __LINE__);
		}
	}
	
	/* Device Mode[2:0] == 0 :Normal operating Mode*/
	if((data[0] & 0x70) != 0) return false; 

	/*get the number of the touch points*/
	point_num= data[2] & 0x0f;
	//printk("ft5606 rate=%d,%d [%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x]\n",report_rate,point_num,data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13],data[14]);	
	//TPD_DEBUG("point_num =%d\n",point_num);
	//if(point_num == 0) return false;
	//TPD_DEBUG("Procss raw data...\n");
		
	for(i = 0; i < point_num; i++)
	{
		cinfo->p[i] = data[3+6*i] >> 6; //event flag 
		cinfo->id[i] = data[3+6*i+2]>>4; //touch id
		/*get the X coordinate, 2 bytes*/
		high_byte = data[3+6*i];
		high_byte <<= 8;
		high_byte &= 0x0f00;
		low_byte = data[3+6*i + 1];
		cinfo->x[i] = high_byte |low_byte;

		//cinfo->x[i] =  cinfo->x[i] * 480 >> 11; //calibra
		
		/*get the Y coordinate, 2 bytes*/			
		high_byte = data[3+6*i+2];
			
		//TPD_DEBUG(" cinfo->finger_id= %d,high_byte=%d\n", cinfo->finger_id[i],high_byte);	
		high_byte <<= 8;
		high_byte &= 0x0f00;
		low_byte = data[3+6*i+3];
		cinfo->y[i] = high_byte |low_byte;

		//cinfo->y[i]=  cinfo->y[i] * 800 >> 11;
		
		cinfo->count++;

		#ifdef FTS_PRESSURE
		cinfo->au8_touch_weight[i]=(data[3+6 * i+4]*5/2); //data[3+6 * i+4]
		cinfo->au8_touch_area[i]=((data[3+6 * i+5]&0xf0) >> 2);//data[3+6 * i+5] >> 4
		#endif
	}
	//TPD_DEBUG(" cinfo->x[0] = %d, cinfo->y[0] = %d, cinfo->p[0] = %d\n", cinfo->x[0], cinfo->y[0], cinfo->p[0]);	
	//TPD_DEBUG(" cinfo->x[1] = %d, cinfo->y[1] = %d, cinfo->p[1] = %d\n", cinfo->x[1], cinfo->y[1], cinfo->p[1]);		
	//TPD_DEBUG(" cinfo->x[2]= %d, cinfo->y[2]= %d, cinfo->p[2] = %d\n", cinfo->x[2], cinfo->y[2], cinfo->p[2]);	
	//printk("ft5606 point %d[%d,%d,%d,%d]\n",point_num,cinfo->x[0],cinfo->y[0],cinfo->au8_touch_weight[0],cinfo->au8_touch_area[0]);
		  
	return true;

 };

#ifdef FTS_GESTRUE
//extern int fetch_object_sample(unsigned char *buf,short pointnum);
//extern void init_para(int x_pixel,int y_pixel,int time_slot,int cut_x_pixel,int cut_y_pixel);

static void check_gesture(int gesture_id)
{
	
    printk("[ft5336_dc]kaka gesture_id==%d\n ",gesture_id);
    
	switch(gesture_id)
	{
		case GESTURE_LEFT:
			input_report_key(tpd->dev, KEY_LEFT, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_LEFT, 0);
			input_sync(tpd->dev);
			break;
		case GESTURE_RIGHT:
			input_report_key(tpd->dev, KEY_RIGHT, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_RIGHT, 0);
			input_sync(tpd->dev);			    
			break;
		case GESTURE_UP:
			input_report_key(tpd->dev, KEY_UP, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_UP, 0);
			input_sync(tpd->dev);			    
			break;
		case GESTURE_DOWN:
			input_report_key(tpd->dev, KEY_DOWN, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_DOWN, 0);
			input_sync(tpd->dev);		    
			break;
		case GESTURE_DOUBLECLICK:
			if(GestrueEnable)
			{
				//input_report_key(tpd->dev, KEY_U, 1);
				input_report_key(tpd->dev, KEY_POWER, 1);
				input_sync(tpd->dev);
				//input_report_key(tpd->dev, KEY_U, 0);
				input_report_key(tpd->dev, KEY_POWER, 0);
				input_sync(tpd->dev);
                custom_vibration_enable(50);
			}
			break;
		case GESTURE_O:
			input_report_key(tpd->dev, KEY_O, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_O, 0);
			input_sync(tpd->dev);
			break;
		case GESTURE_W:
			input_report_key(tpd->dev, KEY_W, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_W, 0);
			input_sync(tpd->dev);			    
			break;
		case GESTURE_M:
			input_report_key(tpd->dev, KEY_M, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_M, 0);
			input_sync(tpd->dev);			    
			break;
		case GESTURE_E:
			input_report_key(tpd->dev, KEY_E, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_E, 0);
			input_sync(tpd->dev);    
			break;
		case GESTURE_C:
			input_report_key(tpd->dev, KEY_C, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_C, 0);
			input_sync(tpd->dev);
			break;
		case GESTURE_S:
			input_report_key(tpd->dev, KEY_S, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_S, 0);
			input_sync(tpd->dev);
			break;
		case GESTURE_V:
			input_report_key(tpd->dev, KEY_V, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_V, 0);
			input_sync(tpd->dev);
			break;
		case GESTURE_Z:
			input_report_key(tpd->dev, KEY_Z, 1);
			input_sync(tpd->dev);
			input_report_key(tpd->dev, KEY_Z, 0);
			input_sync(tpd->dev);
			break;
		default:
		
			break;
	}

}

static int ft5x0x_read_Touchdata(void)
{
    unsigned char buf[FTS_GESTRUE_POINTS * 2] = { 0 };
    int ret = -1;
    int i = 0;
    buf[0] = 0xd3;
    int gestrue_id = 0;
    short pointnum = 0;
    u8 report_rate =0;
	
    //ret = fts_i2c_Read(i2c_client, buf, 1, buf, FTS_GESTRUE_POINTS_HEADER);
    ret = i2c_smbus_read_i2c_block_data(i2c_client, 0xd3, FTS_GESTRUE_POINTS_HEADER, buf);
	
    if (ret < 0)
    {
        printk( "[ft5336_dc]%s read touchdata failed.\n", __func__);
        return ret;
    }
	printk("[ft5336_dc][%s:%d]buf=0x%x,0x%x\n",__func__,__LINE__,buf[0],buf[1]);
    /* FW ?±?ó??3?ê?ê? */
    if (0x24 == buf[0])
    {
        gestrue_id = 0x24;
        check_gesture(gestrue_id);
        return -1;
    }
/*
    pointnum = (short)(buf[1]) & 0xff;
    buf[0] = 0xd3;

    ret = fts_i2c_Read(i2c_client, buf, 1, buf, (pointnum * 4 + 2));
    if (ret < 0)
    {
        printk( "[ft5336_dc]%s read touchdata failed.\n", __func__);
        return ret;
    }

    gestrue_id = fetch_object_sample(buf, pointnum);
   
    for(i = 0;i < pointnum;i++)
    {
    	printk("[ft5336_dc][%s:%d]buf[%d,%d,%d,%d]=[0x%x,0x%x,0x%x,0x%x]\n",__func__,__LINE__,
			0 + (4 * i),1 + (4 * i),2 + (4 * i),3 + (4 * i),
			buf[0 + (4 * i)],buf[1 + (4 * i)],buf[2 + (4 * i)],buf[3 + (4 * i)]);
        coordinate_x[i] =  (((s16) buf[0 + (4 * i)]) & 0x0F) <<
            8 | (((s16) buf[1 + (4 * i)])& 0xFF);
        coordinate_y[i] = (((s16) buf[2 + (4 * i)]) & 0x0F) <<
            8 | (((s16) buf[3 + (4 * i)]) & 0xFF);
    }
	check_gesture(gestrue_id);
*/
    return -1;
}
#endif


static int touch_event_handler(void *unused)
{
  
	struct touch_info cinfo, pinfo;
	int i=0;
	#ifdef FTS_GESTRUE
	u8 state;
	#endif

	struct sched_param param = { .sched_priority = RTPM_PRIO_TPD };
	sched_setscheduler(current, SCHED_RR, &param);
 
	do
	{
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); 
		set_current_state(TASK_INTERRUPTIBLE); 
		wait_event_interruptible(waiter,tpd_flag!=0);
						 
		tpd_flag = 0;
			 
		set_current_state(TASK_RUNNING);

		#ifdef FTS_GESTRUE
		i2c_smbus_read_i2c_block_data(i2c_client, 0xd0, 1, &state);
		// if((get_suspend_state() == PM_SUSPEND_MEM) && (state ==1))
		printk("[ft5336_dc][%s:%d]state=%d\n",__func__,__LINE__,state);
		if(state ==1)
		{
			ft5x0x_read_Touchdata();
			continue;
		}
		#endif

		 

		if (tpd_touchinfo(&cinfo, &pinfo)) 
		{
			TPD_DEBUG("point_num = %d\n",point_num);
			TPD_DEBUG_SET_TIME;
			if(point_num >0) 
			{
			    for(i =0; i<point_num; i++)//only support 10 point
			    {
					#ifdef FTS_PRESSURE
					tpd_down(cinfo.x[i], cinfo.y[i], cinfo.id[i], cinfo.au8_touch_weight[i], cinfo.au8_touch_area[i]);
					#else
					tpd_down(cinfo.x[i], cinfo.y[i], cinfo.id[i]);
					#endif
			    }
			    input_sync(tpd->dev);
			}

			else  
			{
				tpd_up(cinfo.x[0], cinfo.y[0], 0);
				//TPD_DEBUG("release --->\n"); 
				//input_mt_sync(tpd->dev);
				input_sync(tpd->dev);
			}
		}

		if(tpd_mode==12)
		{
			//power down for desence debug
			//power off, need confirm with SA
#ifdef TPD_POWER_SOURCE_CUSTOM
			hwPowerDown(TPD_POWER_SOURCE_CUSTOM,  "TP");
#else
			hwPowerDown(MT65XX_POWER_LDO_VGP2,  "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
			hwPowerDown(TPD_POWER_SOURCE_1800,  "TP");
#endif    
			msleep(20);
          
        	}

	}while(!kthread_should_stop());
 
	return 0;
 }
 
static int tpd_detect (struct i2c_client *client, struct i2c_board_info *info) 
{
	strcpy(info->type, TPD_DEVICE);	
	return 0;
}
 
static void tpd_eint_interrupt_handler(void)
{
	TPD_DEBUG("TPD interrupt has been triggered\n");
	TPD_DEBUG_PRINT_INT;
	tpd_flag = 1;
	wake_up_interruptible(&waiter); 
}
static int ft5316_config_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *ptr = page;
	int i = 0;
	//char registerdata[40];
	ptr += sprintf( ptr, "==== ft5316 config init value====\n" );
	ptr += sprintf( ptr, "fw_version %x\n", ft5x0x_read_fw_ver());
	ptr += sprintf( ptr, "id_version %x\n", ft5x0x_read_ID_ver());
/*	i2c_smbus_read_i2c_block_data(i2c_client, 0x00, 8, &(registerdata[0]));
	i2c_smbus_read_i2c_block_data(i2c_client, 0x00+8, 8, &(registerdata[8]));
	i2c_smbus_read_i2c_block_data(i2c_client, 0x00+16, 8, &(registerdata[16]));
	i2c_smbus_read_i2c_block_data(i2c_client, 0x00+24, 8, &(registerdata[24]));
	i2c_smbus_read_i2c_block_data(i2c_client, 0x00+32, 8, &(registerdata[32]));
	for(;i<40;i++)
	ptr += sprintf( ptr, "register%d %x\n", i,registerdata[i]);
*///wuboadd debug
	ptr += sprintf( ptr, "autocali_status=%x\n", autocali_status);
	*eof = 1;
	ptr += sprintf( ptr, "autocali_status_step1 =%x\n", autocali_status_step1);
	ptr += sprintf( ptr, "autocali_status_step2 =%x\n", autocali_status_step2);
	ptr += sprintf( ptr, "cali_83_status_check =%x\n", cali_83_status_check);
	ptr += sprintf( ptr, "cali_84_status_check =%x\n", cali_84_status_check);

	return ( ptr - page );
}
static int __devinit tpd_probe(struct i2c_client *client, const struct i2c_device_id *id)
{	 
	int retval = TPD_OK;
#ifdef ESD_CHECK	
	int ret;
#endif
	char data;
	u8 report_rate=0;
	int err=0;
	int reset_count = 0;

reset_proc:   
	i2c_client = client;

   
	//power on, need confirm with SA
#ifdef TPD_POWER_SOURCE_CUSTOM
    	hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
#else
    	hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
    	hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif    

	#ifdef TPD_CLOSE_POWER_IN_SLEEP	 
	hwPowerDown(TPD_POWER_SOURCE,"TP");
	hwPowerOn(TPD_POWER_SOURCE,VOL_3300,"TP");
	msleep(100);
	#else
	mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
	msleep(10);  
		
	mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
   	mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
   	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
	msleep(200);
	#endif

	mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
	mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(GPIO_CTP_EINT_PIN, GPIO_PULL_UP);
 
	//mt65xx_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_SENSITIVE);
	//mt65xx_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_DEBOUNCE_CN);
	mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_TYPE, tpd_eint_interrupt_handler, 1);
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
 
	msleep(100);
#ifdef CONFIG_SUPPORT_FTS_CTP_UPG
	CTPI2CDMABuf_va = (u8 *)dma_alloc_coherent(NULL, 4096, &CTPI2CDMABuf_pa, GFP_KERNEL);
    	if(!CTPI2CDMABuf_va)
	{
    		printk("[TSP] dma_alloc_coherent error\n");
	}
#endif		
	if((i2c_smbus_read_i2c_block_data(i2c_client, 0x00, 1, &data))< 0)
	{
		TPD_DMESG("I2C transfer error, line: %d\n", __LINE__);
#ifdef TPD_RESET_ISSUE_WORKAROUND
		if ( reset_count < TPD_MAX_RESET_COUNT )
		{
			reset_count++;
			goto reset_proc;
		}
#endif
		return -1; 
	}

	//set report rate 80Hz
	report_rate = 0x8; 
	if((i2c_smbus_write_i2c_block_data(i2c_client, 0x88, 1, &report_rate))< 0)
	{
	    if((i2c_smbus_write_i2c_block_data(i2c_client, 0x88, 1, &report_rate))< 0)
	    {
		   TPD_DMESG("I2C read report rate error, line: %d\n", __LINE__);
	    }
		   
	}

	tpd_load_status = 1;
	
	#ifdef FTS_GESTRUE
	//init_para(720,1280,60,0,0);
	input_set_capability(tpd->dev, EV_KEY, KEY_POWER);
	//fts_write_reg(i2c_client, 0xd0, 0x01);
	#endif
	#ifdef FTS_PRESSURE
	input_set_abs_params(tpd->dev, ABS_MT_PRESSURE, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(tpd->dev, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	#endif
		

	ft5316_config_proc = create_proc_entry( "ft5606_config", 0666, NULL);
	 if ( ft5316_config_proc == NULL )
		{
			TPD_DEBUG("create_proc_entry %s failed\n", "ft5316_config" );
		}
		else
		{
			ft5316_config_proc->read_proc = ft5316_config_read_proc;
			//gt813_config_proc->write_proc = gt813_config_write_proc;
		}
	#ifdef VELOCITY_CUSTOM_FT5606
	if((err = misc_register(&tpd_misc_device)))
	{
		printk("mtk_tpd: tpd_misc_device register failed\n");		
	}
	#endif
 #if 0//def CONFIG_SUPPORT_FTS_CTP_UPG
    	printk("[TSP] Step 0:init \n");
	msleep(100);
	fts_ctpm_fw_upgrade_with_i_file();
    	printk("[TSP] Step 8:init stop\n");
	printk("[wj]the version is 0x%02x.\n", ft5x0x_read_fw_ver());
#endif	

#ifdef ESD_CHECK	
	ctp_read_id_workqueue = create_workqueue("ctp_read_id");
	INIT_DELAYED_WORK(&ctp_read_id_work, ESD_read_id_workqueue);
	ret = queue_delayed_work(ctp_read_id_workqueue, &ctp_read_id_work,400); //schedule a work for the first detection					
    	printk("[TSP] ret =%d\n",ret);
#endif		
	#ifdef FTS_CTL_IIC
	if (ft_rw_iic_drv_init(client) < 0)
		dev_err(&client->dev, "%s:[FTS] create fts control iic driver failed\n",
				__func__);
    #endif
	thread = kthread_run(touch_event_handler, 0, TPD_DEVICE);
	if (IS_ERR(thread))
	{ 
		retval = PTR_ERR(thread);
		TPD_DMESG(TPD_DEVICE " failed to create kernel thread: %d\n", retval);
	}

	TPD_DMESG("ft5606 Touch Panel Device Probe %s\n", (retval < TPD_OK) ? "FAIL" : "PASS");
#if defined (CONFIG_SUPPORT_FTS_CTP_UPG)  // 苏 勇 2013年11月19日 19:48:27
	atomic_set(&upgrading, 0);
#endif /* CONFIG_SUPPORT_FTS_CTP_UPG */
   return 0;
   
 }

 static int __devexit tpd_remove(struct i2c_client *client)
 
 {
        int err;
	TPD_DEBUG("TPD removed\n");
#ifdef CONFIG_SUPPORT_FTS_CTP_UPG
	 
	if(CTPI2CDMABuf_va)
	{
		dma_free_coherent(NULL, 4096, CTPI2CDMABuf_va, CTPI2CDMABuf_pa);
		CTPI2CDMABuf_va = NULL;
		CTPI2CDMABuf_pa = 0;
	}
#endif	
#ifdef ESD_CHECK	
	destroy_workqueue(ctp_read_id_workqueue);
#endif	
#ifdef FTS_CTL_IIC
	ft_rw_iic_drv_exit();
#endif	
   return 0;
 }
 
 
static int tpd_local_init(void)
{
 
    TPD_DMESG("Focaltech FT5606 I2C Touchscreen Driver (Built %s @ %s)\n", __DATE__, __TIME__);
 
 
    if(i2c_add_driver(&tpd_i2c_driver)!=0)
    {
  	TPD_DMESG("ft5606 unable to add i2c driver.\n");
      	return -1;
    }
    if(tpd_load_status == 0) 
    {
    	TPD_DMESG("ft5606 add error touch panel driver.\n");
    	i2c_del_driver(&tpd_i2c_driver);
    	return -1;
    }
	
#ifdef TPD_HAVE_BUTTON     
    tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);// initialize tpd button data
#endif   
  
#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))    
    TPD_DO_WARP = 1;
    memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT*4);
    memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT*4);
#endif 

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
    memcpy(tpd_calmat, tpd_def_calmat_local, 8*4);
    memcpy(tpd_def_calmat, tpd_def_calmat_local, 8*4);	
#endif  
    TPD_DMESG("end %s, %d\n", __FUNCTION__, __LINE__);  
    tpd_type_cap = 1;

    return 0; 
}

static void tpd_resume( struct early_suspend *h )
{
   	//int retval = TPD_OK;
   	char data;
 
   	TPD_DMESG("TPD wake up\n");
#if defined (CONFIG_SUPPORT_FTS_CTP_UPG)  // 苏 勇 2013年11月19日 19:53:21
   	if(1 == atomic_read(&upgrading))
	{
        TPD_DEBUG("======== Resume:atomic_read(&upgrading)=1 ========\n");
		return;
	}
#endif  /* CONFIG_SUPPORT_FTS_CTP_UPG */

#ifdef FTS_GESTRUE
	if(GestrueEnable)
	{
		#if 0
		fts_write_reg(i2c_client,0xD0,0x00);
		fts_write_reg(i2c_client,0xD1,0x00);
		fts_write_reg(i2c_client,0xD2,0x00);
		#else
		ft5x0x_write_reg(0xD0,0x00);
		ft5x0x_write_reg(0xD1,0x00);
		ft5x0x_write_reg(0xD2,0x00);
		TPD_DMESG("==== 1.TPD wakeup doubletap ====\n");
		#endif
	}
	else
	{
		#ifdef TPD_POWER_SOURCE_CUSTOM
		hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
		#else
		hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP");
		#endif
		TPD_DEBUG("======== GestrueEnable=0 PowerOn!!! ========\n");
	}
	//return;
#else

#ifdef TPD_CLOSE_POWER_IN_SLEEP	
	hwPowerOn(TPD_POWER_SOURCE,VOL_3300,"TP"); 
#else
	discard_resume_first_eint = KAL_TRUE;

	//power on, need confirm with SA
#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
#else
	hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif    
	//msleep(100);

	mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
	msleep(10);  
	mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
#endif
	msleep(200);//add this line 
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);  
 
#endif

#ifdef ESD_CHECK	
	msleep(1);  
	queue_delayed_work(ctp_read_id_workqueue, &ctp_read_id_work,400); //schedule a work for the first detection					
#endif
	
	msleep(20);
	if((i2c_smbus_read_i2c_block_data(i2c_client, 0x00, 1, &data))< 0)
	{
		TPD_DMESG("resume I2C transfer error, line: %d\n", __LINE__);

	}
	tpd_halt = 0;//add this line 
	tpd_up(0,0,0);
	input_sync(tpd->dev);
	TPD_DMESG("TPD wake up done\n");
	 //return retval;
 }

static void tpd_suspend( struct early_suspend *h )
{
	// int retval = TPD_OK;
	static char data = 0x3;

#if defined (CONFIG_SUPPORT_FTS_CTP_UPG)  // 苏 勇 2013年11月19日 19:53:30
	if(1 == atomic_read(&upgrading))
	{
        TPD_DEBUG("======== Suspend: atomic_read(&upgrading)=1 ========\n");
		return;
	}
#endif /* CONFIG_SUPPORT_FTS_CTP_UPG */

#ifdef ESD_CHECK	
 	cancel_delayed_work_sync(&ctp_read_id_work);
#endif
	TPD_DMESG("TPD enter sleep\n");
	tpd_halt = 1; //add this line 

#ifdef FTS_GESTRUE
	if(GestrueEnable)
	{
		#if 0
		fts_write_reg(i2c_client, 0xd0, 0x01);
		fts_write_reg(i2c_client, 0xd1, 0x1f);
		fts_write_reg(i2c_client, 0xd2, 0x1f);
		#else
		ft5x0x_write_reg(0xd0, 0x01);
		ft5x0x_write_reg(0xd1, 0x1f);
		ft5x0x_write_reg(0xd2, 0x1f);
		TPD_DMESG("==== 2.TPD enter doubletap ====\n");
		#endif
	}
	else
	{
		#ifdef TPD_POWER_SOURCE_CUSTOM
		hwPowerDown(TPD_POWER_SOURCE_CUSTOM, "TP");
		#else
		hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
		#endif
		TPD_DEBUG("======== GestrueEnable=0 PowerDown!!! ========\n");    
	}
    return;
#endif 

	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
#ifdef TPD_CLOSE_POWER_IN_SLEEP	
	hwPowerDown(TPD_POWER_SOURCE,"TP");
	TPD_DEBUG("======== TPD_CLOSE_POWER_IN_SLEEP ========\n");
#else
	i2c_smbus_write_i2c_block_data(i2c_client, 0xA5, 1, &data);  //TP enter sleep mode
    //power down, need confirm with SA
	TPD_DEBUG("======== !TPD_CLOSE_POWER_IN_SLEEP ========\n");    
#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerDown(TPD_POWER_SOURCE_CUSTOM,  "TP");
#else
	hwPowerDown(MT65XX_POWER_LDO_VGP2,  "TP");
#endif
#ifdef TPD_POWER_SOURCE_1800
	hwPowerDown(TPD_POWER_SOURCE_1800,  "TP");
#endif

#endif 
	TPD_DMESG("TPD enter sleep done\n");
	//return retval;
 } 




static ssize_t show_chipinfo(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct i2c_client *client =i2c_client;
	unsigned char ver=0;
	unsigned char id=0;
	unsigned char doubleclick = 0;
	unsigned char doubleclickstr[2][10]={" "," (dc)"};
	if(NULL == client)
	{
		printk("i2c client is null!!\n");
		return 0;
	}
	//printk("[TSP]ID_ver=%x, fw_ver=%x\n", ft5x0x_read_ID_ver(), ft5x0x_read_fw_ver());
	//ft5x0x_read_reg(client,TOUCH_FMV_ID,&ver);
	//return sprintf(buf, "[ft5606] ID_ver=%x,fw_ver=%x\n", ft5x0x_read_ID_ver(), ft5x0x_read_fw_ver()); 
	id=ft5x0x_read_ID_ver();
	ver=ft5x0x_read_fw_ver();
	#ifdef FTS_GESTRUE
	doubleclick = ft5x0x_read_doubleclick_flag();
	#endif
	// 为了配合后续的处理,版本信息的应该按照id: ver: ic: vendor:进行处理,请都用小写 苏 勇 2013年11月07日 09:08:34
	switch (id)
	{
		case 0x5a: // 信利 苏 勇 2013年11月13日 09:48:12
			#ifdef FTS_GESTRUE
			return sprintf(buf,"ID:0x%x VER:0x%x IC:ft5606 VENDOR:truely%s\n",id, ver, doubleclickstr[doubleclick]);	
			#else
			return sprintf(buf,"ID:0x%x VER:0x%x IC:ft5606 VENDOR:truely\n",id, ver);
			#endif
			break;
		case 0x55: // tianma 20140529 phil added
			#ifdef FTS_GESTRUE
			return sprintf(buf,"ID:0x%x VER:0x%x IC:ft5606 VENDOR:tianma%s\n",id, ver, doubleclickstr[doubleclick]);	
			#else
			return sprintf(buf,"ID:0x%x VER:0x%x IC:ft5606 VENDOR:tianma\n",id, ver);
			#endif
			break;
		default:
			#ifdef FTS_GESTRUE
			return sprintf(buf,"ID:0x%x VER:0x%x IC:ft5606 VENDOR:ckt%s\n",id, ver, doubleclickstr[doubleclick]);	
			#else
			return sprintf(buf,"ID:0x%x VER:0x%x IC:ft5606 VENDOR:ckt\n",id, ver);
			#endif
			break;
	}

}

static DEVICE_ATTR(chipinfo, 0444, show_chipinfo, NULL);


#ifdef FTS_GESTRUE
static ssize_t show_control_double_tap(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct i2c_client *client =i2c_client;
	
	return sprintf(buf, "gestrue state:%s \n",GestrueEnable==0?"Disable":"Enable");
}

static ssize_t store_control_double_tap(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
    char *pvalue = NULL;
    if(buf != NULL && size != 0)
    {
        printk("[ft5336]store_control_double_tap buf is %s and size is %d \n",buf,size);
        GestrueEnable = simple_strtoul(buf,&pvalue,16);
        printk("[ft5336] store_control_double_tap : %s \n",GestrueEnable==0?"Disable":"Enable");        
    }        
    return size;
}


static DEVICE_ATTR(control_double_tap, 0664, show_control_double_tap, store_control_double_tap);
#endif

static const struct device_attribute * const ctp_attributes[] = {
	&dev_attr_chipinfo
#ifdef FTS_GESTRUE
	,&dev_attr_control_double_tap
#endif
};


 static struct tpd_driver_t tpd_device_driver = {
		 .tpd_device_name = "FT5606",
		 .tpd_local_init = tpd_local_init,
		 .suspend = tpd_suspend,
		 .resume = tpd_resume,
		 .attrs=
		 {
			.attr=ctp_attributes,
			#ifdef FTS_GESTRUE
			.num=2
			#else
			.num=1
			#endif
		 },
#ifdef TPD_HAVE_BUTTON
		 .tpd_have_button = 1,
#else
		 .tpd_have_button = 0,
#endif		
 };

 
/* called when loaded into kernel */
#if defined(CONFIG_SUPPORT_FTS_CTP_UPG)
#if 1
static ssize_t tp_test(struct kobject *kobj,
			struct bin_attribute *attr,
			char *buf, loff_t off, size_t count)
{
	uint16_t val;
	printk("======== TP Calibrating... ========\n");
	fts_ctpm_auto_clb();
	printk("======== TP Calibration Done ========\n");
    
	return count;
}
#else
static ssize_t tp_test(struct kobject *kobj,
			struct bin_attribute *attr,
			char *buf, loff_t off, size_t count)
{
		uint16_t val;
		printk("tp_test\n");
		if(fts_ctpm_fw_upgrade_with_i_file()!=0){
		TPD_DMESG(TPD_DEVICE " luosen failed to upgrade firmware, line: %d\n", __LINE__);
	}
	return count;
}
#endif
static ssize_t tp_read(struct kobject *kobj,
			struct bin_attribute *attr, 
			char *buf, loff_t off, size_t count)
{
	printk("tp_read!!!!!\n");
	int i=300;
	while(i>0)
	{
	mdelay(100);
	i--;
	}
	return count;
}
static struct bin_attribute tp_mode_attr = {
	.attr = {
		.name = "tp",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = 4,
	.read = tp_read,
	.write = tp_test,
};
#endif


static int __init tpd_driver_init(void) {
	printk("MediaTek FT5606 touch panel driver init\n");
#if defined(CONFIG_SUPPORT_FTS_CTP_UPG)	 
	int ret;
	ret = sysfs_create_bin_file(&(module_kset->kobj), &tp_mode_attr);
	if (ret) {
		printk(KERN_ERR "<CTP> Failed to create sys file\n");
		return -ENOMEM;
	}
#endif	

	 
	i2c_register_board_info(0, &ft5606_i2c_tpd, 1);
	if(tpd_driver_add(&tpd_device_driver) < 0)
		TPD_DMESG("add FT5606 driver failed\n");
	return 0;
 }
 
/* should never be called */
static void __exit tpd_driver_exit(void) {
	 TPD_DMESG("MediaTek FT5606 touch panel driver exit\n");
	 //input_unregister_device(tpd->dev);
	 tpd_driver_remove(&tpd_device_driver);
}

module_init(tpd_driver_init);
module_exit(tpd_driver_exit);


