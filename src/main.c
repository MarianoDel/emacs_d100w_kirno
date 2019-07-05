//-----------------------------------------------------
// #### D100W PROJECT  F030 - Custom Board ####
// ##
// ## @Author: Med
// ## @Editor: Emacs - ggtags
// ## @TAGS:   Global
// ## @CPU:    STM32F030
// ##
// #### MAIN.C ########################################
//-----------------------------------------------------

/* Includes ------------------------------------------------------------------*/
#include "stm32f0xx.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpio.h"
#include "tim.h"
#include "uart.h"
#include "hard.h"

#include "core_cm0.h"
#include "adc.h"
#include "dma.h"
#include "flash_program.h"
#include "dsp.h"
#include "it.h"
#include "sync.h"


// Externals -----------------------------------------------

// -- Externals from or for Serial Port --------------------
// volatile unsigned char tx1buff [SIZEOF_DATA];
// volatile unsigned char rx1buff [SIZEOF_DATA];
// volatile unsigned char usart1_have_data = 0;

// -- Externals from or for the ADC ------------------------
volatile unsigned short adc_ch [ADC_CHANNEL_QUANTITY];
volatile unsigned char seq_ready = 0;

// -- Externals for the timers -----------------------------
volatile unsigned short timer_led = 0;

// -- Externals used for analog or digital filters ---------
// volatile unsigned short take_temp_sample = 0;




// Globals -------------------------------------------------
volatile unsigned char overcurrent_shutdown = 0;
// volatile short d = 0;
// short ez1 = 0;
// short ez2 = 0;
// // unsigned short dmax = 0;
// unsigned short last_d = 0;
// #define DELTA_D    2


// ------- de los timers -------
volatile unsigned short wait_ms_var = 0;
volatile unsigned short timer_standby;
//volatile unsigned char display_timer;
// volatile unsigned short timer_meas;
volatile unsigned char timer_filters = 0;
// volatile unsigned short dmax_permited = 0;



// Module Functions ----------------------------------------
void TimingDelay_Decrement (void);
// extern void EXTI4_15_IRQHandler(void);



//-------------------------------------------//
// @brief  Main program.
// @param  None
// @retval None
//------------------------------------------//
int main(void)
{
    unsigned char i;
    unsigned short ii;

    driver_states_t driver_state = AUTO_RESTART;
    unsigned char soft_start_cnt = 0;
    short d = 0;
    short ez1 = 0;
    short ez2 = 0;


    //GPIO Configuration.
    GPIO_Config();

    //ACTIVAR SYSTICK TIMER
    if (SysTick_Config(48000))
    {
        while (1)	/* Capture error */
        {
            if (LED)
                LED_OFF;
            else
                LED_ON;

            for (i = 0; i < 255; i++)
            {
                asm (	"nop \n\t"
                        "nop \n\t"
                        "nop \n\t" );
            }
        }
    }

//---------- Pruebas de Hardware --------//    
    // EXTIOff ();
    
    TIM_3_Init();    //Used for mosfet channels control and ADC synchro

    // TIM_16_Init();    //free running with tick: 1us
    // TIM16Enable();
    // TIM_17_Init();    //with int, tick: 1us
    MA32Circular_Reset();
    
    CTRL_MOSFET(DUTY_NONE);
    
    //ADC and DMA configuration
    AdcConfig();
    DMAConfig();
    DMA1_Channel1->CCR |= DMA_CCR_EN;
    ADC1->CR |= ADC_CR_ADSTART;
    //end of ADC & DMA

#ifdef HARD_TEST_MODE
    ChangeLed(LED_STANDBY);
    while (1)
    {
        if (sequence_ready)
        {
            sequence_ready_reset;
            // if (LED)
            //     LED_OFF;
            // else
            //     LED_ON;
            // CTRL_MOSFET(Vbias_Sense);
            // CTRL_MOSFET(Vup);
            // CTRL_MOSFET(I_Sense);
            // CTRL_MOSFET(Iup);
            CTRL_MOSFET(V220_Sense);
        }
        UpdateLed();
    }
#endif

    
    //--- Production Program ----------
#ifdef DRIVER_MODE
    
    while (1)
    {
        switch (driver_state)
        {
        case POWER_UP:
            //TODO: revisar tambien 220V
            if ((Vbias_Sense > VBIAS_START) && (Vline_Sense > VLINE_START_THRESHOLD))
                driver_state = SOFT_START;
            
            break;

        case SOFT_START:
            if (sequence_ready)
            {
                sequence_ready_reset;
                soft_start_cnt++;

                //reviso no pasarme de corriente de salida
                if (Iup < I_SETPOINT)
                {
                    //hago un soft start respecto de la corriente de salida
                    if (soft_start_cnt > SOFT_START_CNT_ROOF)    //update cada 2ms aprox.
                    {
                        soft_start_cnt = 0;
                    
                        if ((Vup < V_SETPOINT) && (d < DUTY_FOR_DMAX))
                        {
                            d++;
                            CTRL_MOSFET(d);
                        }
                        else
                        {
                            ChangeLed(LED_VOLTAGE_MODE);
                            driver_state = VOLTAGE_MODE;
                        }
                    }
                }                    
                else
                {
                    ChangeLed(LED_CURRENT_MODE);
                    driver_state = CURRENT_MODE;
                }
            }
            break;

        case AUTO_RESTART:
            CTRL_MOSFET(DUTY_NONE);
            d = 0;
            ez1 = 0;
            ez2 = 0;
            ChangeLed(LED_STANDBY);
            driver_state = POWER_UP;
            break;
        
        case VOLTAGE_MODE:
            if (sequence_ready)
            {
                sequence_ready_reset;

                //reviso cambio de modo
                if (Iup < I_SETPOINT)   
                {                
                    d = PID_roof (V_SETPOINT, Vup, d, &ez1, &ez2);
                    if (d)
                    {
                        if (d > DUTY_FOR_DMAX)
                            d = DUTY_FOR_DMAX;
                    }
                    else
                        d = DUTY_NONE;

                    CTRL_MOSFET(d);
                }
                else     //cambio a lazo I
                {
                    ChangeLed(LED_CURRENT_MODE);
                    driver_state = CURRENT_MODE;
                }
            }
            break;

        case CURRENT_MODE:
            if (sequence_ready)
            {
                sequence_ready_reset;

                //reviso cambio de modo
                if (Vup < V_SETPOINT)   
                {                
                    d = PID_roof (I_SETPOINT, Iup, d, &ez1, &ez2);
                    if (d)
                    {
                        if (d > DUTY_FOR_DMAX)
                            d = DUTY_FOR_DMAX;
                    }
                    else
                        d = DUTY_NONE;

                    CTRL_MOSFET(d);
                }
                else     //cambio a lazo V
                {
                    ChangeLed(LED_VOLTAGE_MODE);
                    driver_state = VOLTAGE_MODE;
                }
            }
            break;
            
        case OUTPUT_OVERVOLTAGE:
            if (!timer_standby)
                driver_state = AUTO_RESTART;
            break;

        case INPUT_OVERVOLTAGE:
            if (!timer_standby)
                driver_state = AUTO_RESTART;                
            break;
            
        case OVERCURRENT:
            if (!timer_standby)
                driver_state = AUTO_RESTART;                
            break;

        case BIAS_OVERVOLTAGE:
            if (!timer_standby)
                driver_state = AUTO_RESTART;                
            break;            

        case POWER_DOWN:
            if (!timer_standby)
                driver_state = AUTO_RESTART;                
            break;

        }

        //Cosas que no tienen tanto que ver con las muestras o el estado del programa        
#ifdef USE_LED_FOR_MAIN_STATES
        UpdateLed();
#endif
        
    }
    
#endif    // DRIVER_MODE
    
    return 0;
}

//--- End of Main ---//


void TimingDelay_Decrement(void)
{
    if (wait_ms_var)
        wait_ms_var--;

    if (timer_standby)
        timer_standby--;

    // if (take_temp_sample)
    //     take_temp_sample--;

    // if (timer_meas)
    //     timer_meas--;

    if (timer_led)
        timer_led--;

    if (timer_filters)
        timer_filters--;

#ifdef INVERTER_ONLY_SYNC_AND_POLARITY
    if (timer_no_sync)
        timer_no_sync--;
#endif
    
    // //cuenta de a 1 minuto
    // if (secs > 59999)	//pasaron 1 min
    // {
    // 	minutes++;
    // 	secs = 0;
    // }
    // else
    // 	secs++;
    //
    // if (minutes > 60)
    // {
    // 	hours++;
    // 	minutes = 0;
    // }


}

#define AC_SYNC_Int        (EXTI->PR & 0x00000100)
#define AC_SYNC_Set        (EXTI->IMR |= 0x00000100)
#define AC_SYNC_Reset      (EXTI->IMR &= ~0x00000100)
#define AC_SYNC_Ack        (EXTI->PR |= 0x00000100)

#define AC_SYNC_Int_Rising          (EXTI->RTSR & 0x00000100)
#define AC_SYNC_Int_Rising_Set      (EXTI->RTSR |= 0x00000100)
#define AC_SYNC_Int_Rising_Reset    (EXTI->RTSR &= ~0x00000100)

#define AC_SYNC_Int_Falling          (EXTI->FTSR & 0x00000100)
#define AC_SYNC_Int_Falling_Set      (EXTI->FTSR |= 0x00000100)
#define AC_SYNC_Int_Falling_Reset    (EXTI->FTSR &= ~0x00000100)

#define OVERCURRENT_POS_Int        (EXTI->PR & 0x00000010)
#define OVERCURRENT_POS_Ack        (EXTI->PR |= 0x00000010)
#define OVERCURRENT_NEG_Int        (EXTI->PR & 0x00000020)
#define OVERCURRENT_NEG_Ack        (EXTI->PR |= 0x00000020)

void EXTI4_15_IRQHandler(void)
{
#ifdef WITH_AC_SYNC_INT
    if (AC_SYNC_Int)
    {
        if (AC_SYNC_Int_Rising)
        {
            //reseteo tim
            delta_t2 = TIM16->CNT;
            TIM16->CNT = 0;
            AC_SYNC_Int_Rising_Reset;
            AC_SYNC_Int_Falling_Set;

            SYNC_Rising_Edge_Handler();
        }
        else if (AC_SYNC_Int_Falling)
        {
            delta_t1 = TIM16->CNT;
            AC_SYNC_Int_Falling_Reset;
            AC_SYNC_Int_Rising_Set;
            // ac_sync_int_flag = 1;
            
            SYNC_Falling_Edge_Handler();
        }
        AC_SYNC_Ack;
    }
#endif
    
#ifdef WITH_OVERCURRENT_SHUTDOWN
    if (OVERCURRENT_POS_Int)
    {
        HIGH_LEFT(DUTY_NONE);
        //TODO: trabar el TIM3 aca!!!
        overcurrent_shutdown = 1;
        OVERCURRENT_POS_Ack;
    }

    if (OVERCURRENT_NEG_Int)
    {
        HIGH_RIGHT(DUTY_NONE);
        //TODO: trabar el TIM3 aca!!!
        overcurrent_shutdown = 2;
        OVERCURRENT_NEG_Ack;
    }
#endif
}

//------ EOF -------//
