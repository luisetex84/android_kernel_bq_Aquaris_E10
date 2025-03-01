#include <target/board.h>
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
#define CFG_POWER_CHARGING
#endif
#ifdef CFG_POWER_CHARGING
#include <platform/mt_typedefs.h>
#include <platform/mt_reg_base.h>
#include <platform/mt_pmic.h>
#include <platform/boot_mode.h>
#include <platform/mt_gpt.h>
#include <platform/mt_rtc.h>
#include <platform/mt_disp_drv.h>
#include <platform/mtk_wdt.h>
#include <platform/mtk_key.h>
#include <platform/mt_logo.h>
#include <platform/mt_leds.h>
#include <printf.h>
#include <sys/types.h>
#include <target/cust_battery.h>

#undef printf


/*****************************************************************************
 *  Type define
 ****************************************************************************/
#define BATTERY_LOWVOL_THRESOLD             3450
#ifdef KAITO_PROJECT
#define BATTERY_TEMP_THRESOLD               50
#else
#define BATTERY_TEMP_THRESOLD               61//50
#endif


#define RBAT_PULL_UP_R             16900	
#define RBAT_PULL_DOWN_R		   27000	


#define RBAT_PULL_UP_VOLT          1800
typedef long long       kal_int64;
typedef struct{
    INT32 BatteryTemp;
    INT32 TemperatureR;
}BATT_TEMPERATURE;

 BATT_TEMPERATURE Batt_Temperature_Table[] = {
        {-20,68237},
        {-15,53650},
        {-10,42506},
        { -5,33892},
        {  0,27219},
        {  5,22021},
        { 10,17926},
        { 15,14674},
        { 20,12081},
        { 25,10000},
        { 30,8315},
        { 35,6948},
        { 40,5834},
        { 45,4917},
        { 50,4161},
        { 55,3535},
        { 60,3014}
    };


#define do_div(n,base) ({ \
int __res; \
__res = ((unsigned long) n) % (unsigned) base; \
n = ((unsigned long) n) / (unsigned) base; \
__res; })


static  int abs(int __n) {
    return (__n < 0) ? -__n : __n;
}

int BattThermistorConverTemp(int Res)
{
    int i=0;
    int RES1=0,RES2=0;
    int TBatt_Value=-200,TMP1=0,TMP2=0;

    if(Res>=Batt_Temperature_Table[0].TemperatureR)
    {
        TBatt_Value = -20;
    }
    else if(Res<=Batt_Temperature_Table[16].TemperatureR)
    {
        TBatt_Value = 60;
    }
    else
    {
        RES1=Batt_Temperature_Table[0].TemperatureR;
        TMP1=Batt_Temperature_Table[0].BatteryTemp;

        for(i=0;i<=16;i++)
        {
            if(Res>=Batt_Temperature_Table[i].TemperatureR)
            {
                RES2=Batt_Temperature_Table[i].TemperatureR;
                TMP2=Batt_Temperature_Table[i].BatteryTemp;
                break;
            }
            else
            {
                RES1=Batt_Temperature_Table[i].TemperatureR;
                TMP1=Batt_Temperature_Table[i].BatteryTemp;
            }
        }
        
        TBatt_Value = (((Res-RES2)*TMP1)+((RES1-Res)*TMP2))/(RES1-RES2);
    }

    return TBatt_Value;    
}

int BattVoltToTemp(int dwVolt)
{
    kal_int64 TRes_temp;
    kal_int64 TRes;
    int sBaTTMP = -100;

    // TRes_temp = ((kal_int64)RBAT_PULL_UP_R*(kal_int64)dwVolt) / (RBAT_PULL_UP_VOLT-dwVolt); 
    //TRes = (TRes_temp * (kal_int64)RBAT_PULL_DOWN_R)/((kal_int64)RBAT_PULL_DOWN_R - TRes_temp);

    TRes_temp = (RBAT_PULL_UP_R*(kal_int64)dwVolt); 
    do_div(TRes_temp, (RBAT_PULL_UP_VOLT-dwVolt));

    TRes = (TRes_temp * RBAT_PULL_DOWN_R);
    do_div(TRes, abs(RBAT_PULL_DOWN_R - TRes_temp));

    /* convert register to temperature */
    sBaTTMP = BattThermistorConverTemp((int)TRes);
      
    return sBaTTMP;
}



/*****************************************************************************
 *  Global Variable
 ****************************************************************************/
bool g_boot_reason_change = false;


/*****************************************************************************
 *  Externl Variable
 ****************************************************************************/
extern bool g_boot_menu;

#ifdef MTK_FAN5405_SUPPORT
extern void fan5405_hw_init(void);
extern void fan5405_turn_on_charging(void);
extern void fan5405_dump_register(void);
#endif

#ifdef MTK_BQ24196_SUPPORT
extern void bq24196_hw_init(void);
extern void bq24196_charging_enable(kal_uint32 bEnable);
extern void bq24196_dump_register(void);
extern kal_uint32 bq24196_get_chrg_stat(void);
#endif
#ifdef MTK_BQ24296_SUPPORT
extern void bq24296_hw_init(void);
extern void bq24296_charging_enable(kal_uint32 bEnable);
extern void bq24296_dump_register(void);
extern kal_uint32 bq24296_get_chrg_stat(void);
#endif

void kick_charger_wdt(void)
{
    upmu_set_rg_chrwdt_td(0x0);           // CHRWDT_TD, 4s
    upmu_set_rg_chrwdt_wr(1); 			  // CHRWDT_WR
    upmu_set_rg_chrwdt_int_en(1);         // CHRWDT_INT_EN
    upmu_set_rg_chrwdt_en(1);             // CHRWDT_EN
    upmu_set_rg_chrwdt_flag_wr(1);        // CHRWDT_WR
}

kal_bool is_low_battery(kal_uint32 val)
{
    #ifdef MTK_BQ24196_SUPPORT
    kal_uint32 bq24196_chrg_status;
    
    if(0 == val)
        val = get_i_sense_volt(5);
    #endif

    #ifdef MTK_BQ24296_SUPPORT
    kal_uint32 bq24296_chrg_status;
    
    if(0 == val)
        val = get_i_sense_volt(5);
    #endif
    
    if (val < BATTERY_LOWVOL_THRESOLD)
    {
        printf("%s, TRUE\n", __FUNCTION__);
        return KAL_TRUE;
    }
      
    printf("%s, FALSE\n", __FUNCTION__);
    return KAL_FALSE;
}


kal_bool is_high_bat_tempature(kal_int32 val)
{    
    if (val > BATTERY_TEMP_THRESOLD)
    {
        printf("%s, TRUE\n", __FUNCTION__);
        return KAL_TRUE;
    }
   
    dprintf(INFO, "%s, FALSE\n", __FUNCTION__);
    return KAL_FALSE;
}

void pchr_turn_on_charging (void)
{
	upmu_set_rg_usbdl_set(0);        //force leave USBDL mode
	upmu_set_rg_usbdl_rst(1);		//force leave USBDL mode
	
	kick_charger_wdt();
	
	upmu_set_rg_cs_vth(0xC);    	// CS_VTH, 450mA            
	upmu_set_rg_csdac_en(1);                // CSDAC_EN
	upmu_set_rg_chr_en(1);                  // CHR_EN  
dprintf(INFO, "[mt65xx_bat_init] samwwwwwwwwwwwwwwwwwwwwwwwwwww\n");
                printf("mt65xx_bat_init] samwwwwwwwwwwwwwwwwwwwwwwwwwww\n");

#ifdef MTK_FAN5405_SUPPORT
	fan5405_hw_init();
	fan5405_turn_on_charging();
	fan5405_dump_register();
#endif

#ifdef MTK_BQ24196_SUPPORT
	bq24196_hw_init();
	bq24196_charging_enable(0);  //disable charging with power path
	bq24196_dump_register();
#endif
#ifdef MTK_BQ24296_SUPPORT
	bq24296_hw_init();
	bq24296_charging_enable(1);  //disable charging with power path
	bq24296_dump_register();
#endif
}


void mt65xx_bat_init(void)
{    
		kal_int32 bat_vol;
		
		kal_int32 bat_tempture;
	      kal_int32 bat_temp;
			
		#ifdef MTK_IPO_POWERPATH_SUPPORT
             CHARGER_TYPE CHR_Type_num = CHARGER_UNKNOWN;
             #endif
		// Low Battery Safety Booting
		
		   bat_vol = get_bat_sense_volt(1);
                bat_tempture = get_tbat_volt(1);
                bat_temp=BattVoltToTemp(bat_tempture);
                printf("the bat_vol is %d,the bat_tempture is %d,the bat_temp is %d\n",bat_vol,bat_tempture,bat_temp);

		#if defined(MTK_BQ24196_SUPPORT) ||defined(MTK_BQ24296_SUPPORT)
		bat_vol = get_i_sense_volt(5);
		#endif      

		printf("[mt65xx_bat_init] check VBAT=%d mV with %d mV\n", bat_vol, BATTERY_LOWVOL_THRESOLD);
		
		pchr_turn_on_charging();

		if(g_boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT && (upmu_get_pwrkey_deb()==0) ) {
				printf("[mt65xx_bat_init] KPOC+PWRKEY => change boot mode\n");		
		
				g_boot_reason_change = true;
		}
		rtc_boot_check(false);

	#ifndef MTK_DISABLE_POWER_ON_OFF_VOLTAGE_LIMITATION
    //if (bat_vol < BATTERY_LOWVOL_THRESOLD)
    if (is_low_battery(bat_vol))
    {
        if(g_boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT && upmu_is_chr_det() == KAL_TRUE)
        {
            printf("[%s] Kernel Low Battery Power Off Charging Mode\n", __func__);
            g_boot_mode = LOW_POWER_OFF_CHARGING_BOOT;
            return;
        }
        else
        {
	#ifdef MTK_IPO_POWERPATH_SUPPORT
        //boot linux kernel because of supporting powerpath and using standard AC charger
        if(upmu_is_chr_det() == KAL_TRUE)
       {
        charging_get_charger_type(&CHR_Type_num);
        if(STANDARD_CHARGER == CHR_Type_num)
         {
            return;
         }
        }
       #endif
		
		
            printf("[BATTERY] battery voltage(%dmV) <= CLV ! Can not Boot Linux Kernel !! \n\r",bat_vol);
#ifndef NO_POWER_OFF
            mt6575_power_off();
#endif			
            while(1)
            {
                printf("If you see the log, please check with RTC power off API\n\r");
            }
        }
    }

    if(is_high_bat_tempature(bat_temp))
    {	
	            printf("[BATTERY] battery tempature(%dmV) <= CLV ! Can not Boot Linux Kernel !! \n\r",bat_tempture);
	#ifndef NO_POWER_OFF
            mt6575_power_off();
	#endif			
            while(1)
            {
                printf("If you see the log, please check with RTC power off API\n\r");
            }
        
    }
	#endif
    return;
}

#else

#include <platform/mt_typedefs.h>
#include <platform/mt_reg_base.h>
#include <printf.h>

void mt65xx_bat_init(void)
{
    dprintf(INFO, "[BATTERY] Skip mt65xx_bat_init !!\n\r");
    dprintf(INFO, "[BATTERY] If you want to enable power off charging, \n\r");
    dprintf(INFO, "[BATTERY] Please #define CFG_POWER_CHARGING!!\n\r");
}

#endif
