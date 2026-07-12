/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct __attribute__((packed)) {
    uint16_t voltage;      		// 2 byte 	(mV)
    volatile int16_t current;   // 2 bytes	(mA)
    uint16_t Q_mAh;				// 2 bytes	(mAh)
    uint8_t soc;        		// 1 byte 	(%)
    int8_t temp;       		// 1 byte 	(*C)
} Cell_Data_t;

typedef struct {
    Cell_Data_t cells[3];
    uint16_t balance_value[3];
    float total_Q_As[3];
} BMS_Manager_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SOC_TABLE_SIZE 	11
#define RX_DATA_SIZE	64		//Kích thước cần để cấp phát cho vùng nhớ data_rx
#define DATA_SIZE		128		//Kích thước cần để cấp phát cho vùng nhớ data_tx
#define NOMINAL_Q_mAh 	2500	//Dung lượng danh định của cell là 2500mAh
#define T0				298.15	//Nhiệt độ T0 = 25*C (T0 = 298.15K)
#define R_FIXED			10		//�?iện trở của cầu phân áp cho NTC
#define R_internal		0.05f	//Nội trở của pin

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

CAN_HandleTypeDef hcan;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */

BMS_Manager_t bms = {0};
uint32_t start_time = 0;
uint8_t click_led = 0;
float voltage[3] = {0};


/*
 * Biến dùng cho truy�?n và nhận đữ liệu
 */
CAN_RxHeaderTypeDef RxHeader;
uint32_t TxMailBox = 0;

/*
 * mảng dùng để chứa dữ liệu adc của điện áp, dòng điện và nhiệt độ
 */
uint16_t adc_data[9] = {0};

/*
 * Biến dùng cho tính dung lượng, nhiệt độ, điện áp và dòng điện
 */

int8_t Temperature_ref[] = {-40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30,
							-29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19,
							-18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7,
							-6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
							11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
							26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
							41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
							56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
							71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
							86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
							101, 103, 104, 105, 106, 107, 108, 109, 110};
float Resistor_ref[] = {190.5562, 183.4132, 175.6740, 167.6467, 159.5647, 151.5975, 143.8624,
						136.4361, 129.3641, 122.6678, 116.3519, 110.4098, 104.8272, 99.5847,
						94.6608, 90.0326, 85.6778, 81.5747, 77.7031, 74.0442, 70.5811,
						67.2987, 64.1834, 61.2233, 58.4080, 55.7284, 53.1766, 50.7456,
						48.4294, 46.2224, 44.1201, 42.1180, 40.2121, 38.3988, 36.6746,
						35.0362, 33.4802, 32.0035, 30.6028, 29.2750, 28.0170, 26.8255,
						25.6972, 24.6290, 23.6176, 22.6597, 21.7522, 20.8916, 20.0749,
						19.2988, 18.5600, 18.4818, 18.1489, 17.6316, 16.9917, 16.2797,
						15.5350, 14.7867, 14.0551, 13.3536, 12.6900, 12.0684, 11.4900,
						10.9539, 10.4582, 10.0000, 9.5762, 9.1835, 8.8186, 8.4784, 8.1600,
						7.8608, 7.5785, 7.3109, 7.0564, 6.8133, 6.5806, 6.3570, 6.1418,
						5.9343, 5.7340, 5.5405, 5.3534, 5.1725, 4.9976, 4.8286, 4.6652,
						4.5073, 4.3548, 4.2075, 4.0650, 3.9271, 3.7936, 3.6639, 3.5377,
						3.4146, 3.2939, 3.1752, 3.0579, 2.9414, 2.8250, 2.7762, 2.7179,
						2.6523, 2.5817, 2.5076, 2.4319, 2.3557, 2.2803, 2.2065, 2.1350,
						2.0661, 2.0004, 1.9378, 1.8785, 1.8225, 1.7696, 1.7197, 1.6727,
						1.6282, 1.5860, 1.5458, 1.5075, 1.4707, 1.4352, 1.4006, 1.3669,
						1.3337, 1.3009, 1.2684, 1.2360, 1.2037, 1.1714, 1.1390, 1.1067,
						1.0744, 1.0422, 1.0104, 0.9789, 0.9481, 0.9180, 0.8889, 0.8346,
						0.8099, 0.7870, 0.7665, 0.7485, 0.7334, 0.7214, 0.7130};

/*
 * Mảng chứa các mức điện áp tham chiếu cho SOC
 */
static const float SOC_Voltage_Table[] = {	2.60f,  // 0%
											3.00f,  // 10%
											3.20f,  // 20%
											3.22f,  // 30%
											3.25f,  // 40%
											3.26f,  // 50%
											3.27f,  // 60%
											3.30f,  // 70%
											3.32f,  // 80%
											3.35f,  // 90%
											3.65f	//100%
											};
float soc_init[3] = {0};
float soc_coulomb[3] = {0};
uint8_t soc_initialized = 0;
int16_t Ix[3];
uint16_t pwm[3]={0};
uint16_t avg_volt,min_volt;
int start=0;
uint16_t v_offset[3];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_CAN_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/*
 * Hàm thực thi ngắt
 */
//==================PWM=================================================
void Set_PWM1(float duty)
{
    if(duty > 1000) duty = 1000;
    if(duty < 0) duty = 0;

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3,
                         (uint16_t)(duty * 1));
}

void Set_PWM2(float duty)
{
    if(duty > 1000) duty = 1000;
    if(duty < 0) duty = 0;

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4,
                         (uint16_t)(duty * 1));
}
void Set_PWM3(float duty)
{
	if(duty > 1000) duty = 1000;
	if(duty < 0) duty = 0;
	float c;
	c = 1000 - duty;

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1,
                         (uint16_t)(c * 1));
}
void volt_ctrol(uint8_t id){
	if (bms.cells[id].voltage > (bms.balance_value[0]+300)){
		pwm[id] = 500;
	}
	if (bms.cells[id].voltage > (bms.balance_value[0]+100)){
		pwm[id] = 200;
	}
	if ( bms.cells[id].voltage > 3800){
		pwm[id] = 500;
	}
	if (pwm[id]>500) pwm[id]=500;
	if (pwm[id]<0) pwm[id]=0;

	switch(id)
	    {
	        case 0: Set_PWM1(pwm[id]); break;
	        case 1: Set_PWM2(pwm[id]); break;
	        case 2: Set_PWM3(pwm[id]); break;
	    }
}
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
	uint8_t data_rx[6] = {0};
	if(HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, data_rx)== HAL_OK)
	{
			memcpy(&bms.balance_value[0], &data_rx[0], 2);
			memcpy(&bms.balance_value[1], &data_rx[2], 2);
			memcpy(&bms.balance_value[2], &data_rx[4], 2);
	}
	if (bms.balance_value[2]==1){
	volt_ctrol(0);
	volt_ctrol(1);
	volt_ctrol(2);
	}
	if(!click_led)
	{
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
		click_led = 1;
	}
	else if(click_led == 1)
	{
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
		click_led = 0;
	}
}

/*
 * Hàm tính SOC
 */
float SOC_From_Voltage(float V)
{
	if(V <= SOC_Voltage_Table[0])
		return 0;
	else if(V >= SOC_Voltage_Table[SOC_TABLE_SIZE - 1])
		return 100;
	else
	{
		for(uint8_t i = 1; i < SOC_TABLE_SIZE; i++)
		{
			if(V < SOC_Voltage_Table[i])
			{
				float V1 = SOC_Voltage_Table[i - 1];
				float V2 = SOC_Voltage_Table[i];
				uint8_t SOC1 = (i - 1) * 10;
				uint8_t SOC2 = i * 10;
				return (SOC1 + (((SOC2 - SOC1)*(V - V1))/(V2 - V1)));
			}

		}
	}
	return 100;
}

void SOC_Init()
{
	for(uint8_t i = 0; i < 3; i++)
	{
		float I_A = (float)bms.cells[i].current/1000.0f;
		float V_ocv = (bms.cells[i].voltage / 1000.0f);

		soc_init[i] = SOC_From_Voltage(V_ocv);
		soc_coulomb[i] = soc_init[i];
		bms.cells[i].soc = soc_coulomb[i];

        bms.total_Q_As[i] = (soc_init[i] / 100.0f) * NOMINAL_Q_mAh * 3.6f;
        bms.cells[i].Q_mAh = (uint16_t)(bms.total_Q_As[i] * (1000.0f / 3600.0f));
	}

}

/*
 * Hàm tính toán dung lượng cho cell pin
 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if(!soc_initialized) return;
	for(uint8_t i = 0; i < 3; i++)
	{

		float I_A = (float)bms.cells[i].current / 1000.0f;
		if (I_A <0) I_A =0;
		soc_coulomb[i] += ((I_A * 0.01f) /((float)NOMINAL_Q_mAh * 3.6f)) * 100.0f;

		if(soc_coulomb[i] > 100)		soc_coulomb[i] = 100;
		else if (soc_coulomb[i] < 0) 	soc_coulomb[i] = 0;

		bms.cells[i].soc = (uint8_t)soc_coulomb[i];

		//Dung lượng tích lũy
        bms.total_Q_As[i] += I_A * 0.01f;
        bms.cells[i].Q_mAh = (uint16_t)(bms.total_Q_As[i] * (1000.0f/3600.0f));
        if (bms.cells[i].Q_mAh > 2500) bms.cells[i].Q_mAh=2500;
	}
}

/*
 * Hàm tính nhiệt độ
 */
int8_t Temperature_Function(float R)
{
	for(uint8_t i = 1; i < sizeof(Temperature_ref); i++)
	{
		float R2t = Resistor_ref[i-1];
		float R1t = Resistor_ref[i];
		if(R <= R2t && R >= R1t)
		{
			int8_t T1 = Temperature_ref[i-1];
			int8_t T2 = Temperature_ref[i];
			return (int8_t)(T1 + ((T2 - T1)*((R - R1t)/(R2t - R1t))));
		}
	}

	//Trư�?ng hợp xảy ra lỗi
	return 0xFF;
}

/*
 * Hàm tính nhiệt độ cho từng cell pin
 */
void NTC_Read_Temperature()
{
	for(uint8_t i = 0; i < 3; i++)
	{
		//Chuyển đổi giá trị ADC thành tín hiệu điện áp
		float U_NTC = ((adc_data[i+3] * 3.2)/4095);

		//Tính giá trị R_NTC
		float R_NTC = (float)((R_FIXED * U_NTC)/(3.2 - U_NTC));

		bms.cells[i].temp = Temperature_Function(R_NTC);
	}
}

void SendCellData()
{
  /*
   * Cấu hình cho frame truy�?n dữ liệu
   */
	CAN_TxHeaderTypeDef TxHeader;
	TxHeader.RTR = CAN_RTR_DATA;		/*Chon chế độ truyen dữ liệu*/
	TxHeader.IDE = CAN_ID_STD;			/*Chon ID tiêu chuẩn*/
	TxHeader.DLC = 8;					/*Kích thước của dữ liệu (8 bytes)*/

	for(uint8_t i = 0; i < 3; i++)
	{
		TxHeader.StdId = 0x30 + i;
		while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0);
		HAL_CAN_AddTxMessage(&hcan, &TxHeader, (uint8_t*)&bms.cells[i], &TxMailBox);
	}
}



//====hàm trung bình
uint16_t avg(uint16_t v1,uint16_t v2,uint16_t v3){
	uint16_t a =(v1+v2+v3)/3;
	return  a;
}
//====hàm tìm min
uint16_t min_v(uint16_t v1,uint16_t v2,uint16_t v3){

	uint16_t min =(v1+v2+v3)/3;
	if(min>v1) min=v1;
	if(min>v2) min=v2;
	if(min>v3) min=v3;
	return  min;
}
float get_k1(uint16_t adc)
{
	 if (adc < 30)
	        return 24.66667f;
	    else if (adc < 100)
	        return 14.5f;
	    else if (adc < 155)
	        return 12.129032f;
	    else if (adc < 194)
	        return 11.185567f;
	    else if (adc < 253)
	        return 10.533597f;
	    else if (adc < 400)
	        return 8.75f;
	    else
	        return 7.8f;
}
float get_k2(uint16_t adc){
	 if (adc < 10)
	        return 120.0f;
	    else if (adc < 49)
	        return 45.20408163f;
	    else if (adc < 54)
	        return 30.88888889f;
	    else if (adc < 65)
	        return 24.61538462f;
	    else if (adc < 74)
	        return 23.91891892f;
	    else if (adc < 80)
	        return 12.75f;
	    else
	        return 12.75f;
}
float get_k3(uint16_t adc){
	 if (adc < 28)
	        return 17.85714286f;
	    else if (adc < 87)
	        return 8.620689655f;
	    else if (adc < 147)
	        return 6.462585034f;
	    else if (adc < 156)
	        return 7.692307692f;
	    else if (adc < 226)
	        return 6.194690265f;
	    else if (adc < 337)
	        return 5.637982196f;
	    else
	        return 5.037982196f;
}
//=================== đi�?u khiển dòng bằng pwm


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_CAN_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
	HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);


  /*
   * Bắt đầu giao thức CAN
   */
  HAL_CAN_Start(&hcan);

  /*
   * Kích hoạt ngắt
   */
  HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);

  /*
   * Hàm dùng để tự hiệu chỉnh sai số của bộ ADC
   */
  HAL_ADCEx_Calibration_Start(&hadc1);

  /*
   * Bắt đầu quá trình đ�?c ADC
   */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_data, sizeof(adc_data)/2);

  /*
   * Bắt đầu ngắt timer
   */

  uint32_t SAC_time = HAL_GetTick();

  start_time = HAL_GetTick();
  uint32_t cu_time = HAL_GetTick();
  start =0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  /*
	  	   * Tính dòng điện thực của cell
	  	   */
	  if(HAL_GetTick()-cu_time>500){
				Ix[0] = (adc_data[6] * 3200/4095)-v_offset[0] ;//mA;
				bms.cells[0].current = (int16_t) ((float)Ix[0] * get_k1(Ix[0]));

				Ix[1] = (adc_data[7] * 3200/4095)-v_offset[1] ;//mA
				bms.cells[1].current = (int16_t) ((float)Ix[1] * get_k2(Ix[1]));

				Ix[2] =(adc_data[8] * 3200/4095)- v_offset[2];
				bms.cells[2].current = (int16_t) ((float)Ix[2] * get_k3(Ix[2]));

		 		  cu_time=HAL_GetTick();
		 	  }
		 	  Set_PWM1(pwm[0]);//700
		 	  Set_PWM2(pwm[1]);//900
		 	  Set_PWM3(pwm[2]);//400


		 	  	  /*
		 	  	   * Tính điện áp thực của cell
		 	  	   */
		 	  	  voltage[0] = ((adc_data[0] * 32*492)/4095) ;
		 	  	  voltage[1] = ((adc_data[1] * 32*494)/4095) ;
		 	  	  voltage[2] = ((adc_data[2] * 32*490)/4095) ;
		 	  	  voltage[0] -= voltage[1];
		 	  	  voltage[1] -= voltage[2];
		 		  bms.cells[0].voltage = (uint16_t)(voltage[0] );//mV
		 		  bms.cells[1].voltage = (uint16_t)(voltage[1] );
		 		  bms.cells[2].voltage = (uint16_t)(voltage[2] );

		 		  avg_volt = avg(voltage[0], voltage[1], voltage[2]);
		 		  min_volt = min_v(voltage[0], voltage[1], voltage[2]);
		 	  if(!soc_initialized)
		 	  {
		 		  static uint16_t stable_count = 0;
		 		  if (stable_count++ < 700)
		 		  {
		 			  /*
		 			   * Xác định SOC ban đầu.
		 			   */
		 			  SOC_Init();
		 				v_offset[0] =  (adc_data[6] * 3200/4095);
		 				v_offset[1] =  (adc_data[7] * 3200/4095);
		 				v_offset[2] =  adc_data[8] * 3200/4095;
		 		  }
		 		  else {
		 			  soc_initialized = 1;
		 			  HAL_TIM_Base_Start_IT(&htim3);
		 			  }
		 	  }
	  /*
	   * Tính nhiệt độ thực của cell
	   */
	  NTC_Read_Temperature();
    
	  /*
	   * Truy�?n dữ liệu gồm điện áp, dòng điện, dung lượng và nhiệt độ đến master.
	   */

	  uint32_t now_time = HAL_GetTick();
	  if(now_time - start_time >= 1500)
	  {
		  SendCellData();
		  start_time = now_time;
	  }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 9;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_7;
  sConfig.Rank = ADC_REGULAR_RANK_7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_8;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_9;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief CAN Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 18;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_12TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_3TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = ENABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = ENABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */
  CAN_FilterTypeDef FilterConfig;

  FilterConfig.FilterBank = 0;
  FilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  FilterConfig.FilterIdHigh = 0x10 << 5;
  FilterConfig.FilterIdLow = 0;
  FilterConfig.FilterMaskIdHigh = 0x7FF << 5;
  FilterConfig.FilterMaskIdLow = 0;
  FilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  FilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  FilterConfig.FilterActivation = CAN_FILTER_ENABLE;
  HAL_CAN_ConfigFilter(&hcan, &FilterConfig);

  /* USER CODE END CAN_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 1-1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 1-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 9999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
