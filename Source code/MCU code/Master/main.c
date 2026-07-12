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
#include <stdio.h>
#include <stdlib.h>
#include "string.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct __attribute__((packed)) {
    uint16_t voltage;   	// 2 byte	(mV)
    int16_t current;   		// 2 bytes 	(mA)
    uint16_t Q_mAh;			// 2 bytes	(mAh)
    uint8_t soc;        	// 1 byte 	(%)
    uint8_t temp;       	// 1 byte 	(*C)
} Cell_Data_t;

typedef struct {
    Cell_Data_t cells[3];
    uint16_t I_target[3];
    uint8_t rx_complete;
} BMS_Manager_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DATA_SIZE 		64
#define RX_DATA_SIZE 	128
#define NOMINAL_Q_mAh 	2500	//Dung lượng danh định của cell là 2500mAh
#define Current_LSB 	0.0001f
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

CAN_HandleTypeDef hcan;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/*
 * Khai báo hai struct chứa thông tin v�? các cell
 */
BMS_Manager_t bms1 = {0};
BMS_Manager_t bms2 = {0};

/*
 * Biến dùng cho truy�?n và nhận đữ liệu
 */
CAN_RxHeaderTypeDef RxHeader;
uint32_t TxMailBox = 0;

/*
 * Mảng chứa dữ liệu truy�?n đi thông qua UART
 */
char buffer[512] = {0};
uint32_t uart_tick = 0;
uint32_t can_tick = 0;
uint32_t current_tick = 0;
/*
 * Biến phục vụ quá trình tính toán
 */

uint8_t SOC_target = 0;
uint16_t I_charge = 0;
uint32_t pwm = 719;
int16_t current = 0;
uint8_t SOC_min = 0;

uint16_t adc1,adc2;
float Vin, Vout;
uint16_t pwm1=0;
uint16_t adc_data[2] = {0};
float Vmax = 22.0f;
float Vmin ;
uint8_t pwm_i=0;
float V_lt=8;
uint16_t i_c = 0;
float max,min;
uint8_t i=0;
uint16_t adc1,adc2;
float I_FIX = 0;
int start=0;
float v_offset;
int16_t i6[6];
uint32_t avg_volt,min_volt;

uint8_t rx;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_CAN_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/*
 * Hàm thực thi ngắt
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // rx chứa dữ liệu vừa nhận

        HAL_UART_Receive_IT(&huart1, &rx, 1);
    }
}
//====hàm trung bình

void avg_v(){
	avg_volt = bms1.cells[0].voltage+bms1.cells[1].voltage+bms1.cells[2].voltage;
	avg_volt += bms2.cells[0].voltage+bms2.cells[1].voltage+bms2.cells[2].voltage;
	avg_volt = avg_volt/6;
}
//====hàm tìm min
uint16_t min_v(){

	uint16_t min_v1 = avg_volt;
	for(uint8_t i=0;i<3;i++){
		if(min_v1>bms1.cells[i].voltage) min_v1 = bms1.cells[i].voltage;
	}
	for(uint8_t i=0;i<3;i++){
		if(min_v1>bms2.cells[i].voltage) min_v1 = bms2.cells[i].voltage;
	}
	return  min_v1;
}
uint16_t max_v(){

	uint16_t max_v1 = avg_volt;
	for(uint8_t i=0;i<3;i++){
		if(max_v1 < bms1.cells[i].voltage) max_v1 = bms1.cells[i].voltage;
	}
	for(uint8_t i=0;i<3;i++){
		if(max_v1< bms2.cells[i].voltage) max_v1 = bms2.cells[i].voltage;
	}
	return  max_v1;
}
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
	uint8_t data[8] = {0};
	if(HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, data) == HAL_OK)
	{
		if(RxHeader.StdId == 0x030)
		{
			memcpy(&bms1.cells[0], data, 8);
			bms1.rx_complete |= 0x01;
		}
		else if(RxHeader.StdId == 0x031)
		{
			memcpy(&bms1.cells[1], data, 8);
			bms1.rx_complete |= 0x02;
		}
		else if(RxHeader.StdId == 0x032)
		{
			memcpy(&bms1.cells[2], data, 8);
			bms1.rx_complete |= 0x04;
		}
		if(RxHeader.StdId == 0x020)
		{
			memcpy(&bms2.cells[0], data, 8);
			bms2.rx_complete |= 0x01;
		}
		else if(RxHeader.StdId == 0x021)
		{
			memcpy(&bms2.cells[1], data, 8);
			bms2.rx_complete |= 0x02;
		}
		else if(RxHeader.StdId == 0x022)
		{
			memcpy(&bms2.cells[2], data, 8);
			bms2.rx_complete |= 0x04;
		}
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

	uint8_t data_tx1[6] = {0};
	uint8_t data_tx2[6] = {0};

//	memcpy(&data_tx1[0], &bms1.I_target[0], 2);
//	memcpy(&data_tx1[2], &bms1.I_target[1], 2);
//	memcpy(&data_tx1[4], &bms1.I_target[2], 2);
//
//	memcpy(&data_tx2[0], &bms2.I_target[0], 2);
//	memcpy(&data_tx2[2], &bms2.I_target[1], 2);
//	memcpy(&data_tx2[4], &bms2.I_target[2], 2);
	avg_v();
	bms1.I_target[0] = avg_volt;
	bms1.I_target[1] = min_v();
	bms2.I_target[0] = bms1.I_target[0];
	bms2.I_target[1] = bms1.I_target[1];
	if (start==1) 	bms1.I_target[2] = 1;
	else bms1.I_target[2] = 0;
	bms2.I_target[2]=bms1.I_target[2];
	memcpy(&data_tx1[0], &bms1.I_target[0], 2);
	memcpy(&data_tx1[2], &bms1.I_target[1], 2);
	memcpy(&data_tx1[4], &bms1.I_target[2], 2);

	memcpy(&data_tx2[0], &bms2.I_target[0], 2);
	memcpy(&data_tx2[2], &bms2.I_target[1], 2);
	memcpy(&data_tx2[4], &bms2.I_target[2], 2);
	TxHeader.StdId = 0x10;
	while(HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0);
	HAL_CAN_AddTxMessage(&hcan, &TxHeader, (uint8_t*)data_tx1, &TxMailBox);

	TxHeader.StdId = 0x11;
	while(HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0);
	HAL_CAN_AddTxMessage(&hcan, &TxHeader, (uint8_t*)data_tx2, &TxMailBox);
}

void proccess_data(void)
{
    __disable_irq();

    SOC_min = bms2.cells[0].soc;

    for(uint8_t i=1;i<3;i++)
    {
        if(bms2.cells[i].soc < SOC_min)
            SOC_min = bms2.cells[i].soc;
    }
    for(uint8_t i=1;i<3;i++)
	{
		if(bms1.cells[i].soc < SOC_min)
			SOC_min = bms1.cells[i].soc;
	}
    if(SOC_min <= 5)
       {
           I_charge = 1000;
           SOC_target = 5;
       }
    else if(SOC_min <= 60)
    {
        I_charge = 2000;
        SOC_target = 60;
    }
    else if(SOC_min <= 70)
    {
        I_charge = 1500;
        SOC_target = 70;
    }
    else
    {
        I_charge = 700;
        SOC_target = 80;
    }

    float delta_Q =
            (SOC_target - SOC_min)
            * NOMINAL_Q_mAh/100.0f;

    float t_charge =
            delta_Q / I_charge;

    for(uint8_t i=0;i<3;i++)
    {
        if(bms2.cells[i].soc >= SOC_target)
        {
            bms2.I_target[i] = 0;
        }
        else
        {
            delta_Q =
                (SOC_target - bms2.cells[i].soc)
                * NOMINAL_Q_mAh/100.0f;

            bms2.I_target[i] =
                delta_Q / t_charge;
        }
    }

    bms2.rx_complete = 0;
    for(uint8_t i=0;i<3;i++)
       {
           if(bms1.cells[i].soc >= SOC_target)
           {
               bms1.I_target[i] = 0;
           }
           else
           {
               delta_Q =
                   (SOC_target - bms1.cells[i].soc)
                   * NOMINAL_Q_mAh/100.0f;

               bms1.I_target[i] =
                   delta_Q / t_charge;
           }
       }

       bms1.rx_complete = 0;
    __enable_irq();
}

void read_current(){
	 current = adc_data[1] * 3200/4095;//mV

	if (Vin < 19.5) current = (current- (uint16_t)v_offset)*43;
	else current = (current- (uint16_t)v_offset)*41;
}
//
//void INA219_Init(I2C_HandleTypeDef *hi2c)
//{
//    uint16_t config = 0x399F; // cấu hình phổ biến
//    uint8_t data[2] = {0};
//
//    data[0] = (config >> 8) & 0xFF;
//    data[1] = config & 0xFF;
//
//    HAL_I2C_Mem_Write(hi2c,0x40 << 1, 0x00, I2C_MEMADD_SIZE_8BIT, data, 2, 100);
//}
//
//void INA219_Calibrate(I2C_HandleTypeDef *hi2c)
//{
//    uint16_t calib = 6922;
//    uint8_t data[2] = {0};
//
//    data[0] = (calib >> 8) & 0xFF;
//    data[1] = calib & 0xFF;
//
//    HAL_I2C_Mem_Write(hi2c, 0x40 << 1, 0x05, I2C_MEMADD_SIZE_8BIT, data, 2, 100);
//}
//
//int16_t INA219_ReadRegister(I2C_HandleTypeDef *hi2c, uint8_t reg)
//{
//    uint8_t buffer[2] = {0};
//    HAL_I2C_Mem_Read(hi2c, 0x40 << 1, reg, I2C_MEMADD_SIZE_8BIT, buffer, 2, 100);
//
//    return (int16_t)((buffer[0] << 8) | buffer[1]);
//}
//
//int16_t INA219_ReadCurrent(I2C_HandleTypeDef *hi2c)
//{
//    int16_t raw_current = INA219_ReadRegister(hi2c, 0x04);
//    return (raw_current * Current_LSB * 1000); // mA
//}
void Set_PWM1(float duty)
{
    if(duty > 100) duty = 100;
    if(duty < 0) duty = 0;

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4,
                         (uint16_t)(duty * 1));
}
void Set_PWM_to_i_charge(uint16_t i_ch){
	uint16_t i_slect = i_ch;
	if (i_slect > 3000) i_slect = 3000;
	if (i_slect < 250) i_slect = 250;
	  Vin = ((float)adc_data[0] * 3.2f / 4095.0f)*7.8f;//V
//	  current = adc_data[1] * 32* 100/4095;//mV
//	  current = (686 * current) / 100 + 250;//mA
	  read_current();
	Set_PWM1(pwm_i);
	if ((current < (i_slect))&&(Vin < Vmax)){
//		uint8_t m=0;
//		while ((current < (i_slect))&&(Vin < Vmax)&&(m==0)){
				pwm_i--;
				if (pwm_i < 0) {
					pwm_i = 0;
				}
				Set_PWM1(pwm_i);
				HAL_Delay(10);
//				  Vin = ((float)adc_data[0] * 3.2f / 4095.0f)*7.8f;//V
//				  read_current();
//			}
	}
	else if ((current > (i_slect))&&(Vin < Vmax))
	{
//		uint8_t m=0;
//		while ((current > (i_slect))&&(Vin < Vmax)&&(m==0)){
				pwm_i++;
				Set_PWM1(pwm_i);
				HAL_Delay(10);
				if (pwm_i > 100) {
					pwm_i = 100;
				}
//				  Vin = ((float)adc_data[0] * 3.2f / 4095.0f)*7.8f;//V
//				  read_current();
//			}
	}
	if (pwm_i > 100) pwm_i=100;
	if (pwm_i < 0) pwm_i=0;
	Set_PWM1(pwm_i);
}
float I_Filter(float adc_new)
{
	I_FIX = 0.9f * I_FIX + 0.1f * adc_new;
    return I_FIX;
}
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
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */

  /*
   * Cấu hình cho cảm biến dòng
   */
//  INA219_Init(&hi2c1);
//  INA219_Calibrate(&hi2c1);

  /*
   * Bắt đầu tạo xung pwm
   */
//  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

  /*
   * Bắt đầu giao tiếp CAN
   */
  HAL_CAN_Start(&hcan);
  HAL_UART_Receive_IT(&huart1, &rx, 1);

  /*
   * Kích hoạt ngắt
   */
  HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);

  uart_tick = HAL_GetTick();
  can_tick = HAL_GetTick();
  current_tick = HAL_GetTick();
  /*
   * Bắt đầu quá trình đ�?c ADC
   */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_data, sizeof(adc_data)/2);

  /* Dữ liệu giả*/
//  bms1.I_target[0] = 2340;
//  bms1.I_target[1] = 2120;
//  bms1.I_target[2] = 2430;
//
//  bms2.I_target[0] = 2000;
//  bms2.I_target[1] = 2100;
//  bms2.I_target[2] = 2150;
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);

//  Set_PWM1(80);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1){
//	  HAL_UART_Receive_IT(&huart1, &rx, 1);
//	  HAL_UART_Receive(&huart1, &rx, 2, 100);
	  uint32_t now_time = HAL_GetTick();
	  Vin = ((float)adc_data[0] * 3.2f / 4095.0f)*7.8f;//V
	  v_offset = 2.667f * Vin -11.0f;
	  if (rx == 49) start=1;
	  else start=0;
	  read_current();
	  i6[0]= bms1.cells[0].current;
	  i6[1]= bms1.cells[1].current;
	  i6[2]= bms1.cells[2].current;

	  i6[3]= bms2.cells[0].current;
	  i6[4]= bms2.cells[1].current;
	  i6[5]= bms2.cells[2].current;

//	  current = adc_data[1] * 3200/4095;//mV
//
//	  if (Vin < 19.5) current = (current- (uint16_t)v_offset)*43;
//	  else current = (current- (uint16_t)v_offset)*41;


//	  Set_PWM_to_i_charge(i_c);
//	  Set_PWM1(pwm1);
	  if (SOC_min < 80) Set_PWM_to_i_charge(I_charge);
	  else if (SOC_min < 95) Set_PWM1(5);// cố định áp khi cell thấp nhất gần đầy
	  else  start=0;
	  if (max_v()>4000){

		  HAL_Delay(10000);
		  start = 0;
	  }
//	  i++;
//	  if(current > max)  max = current;
//	  if(current < min)  min = current;
//	  if (i == 100) {
//	  		  max = 0;
//	  		  min = I_charge;
//	  		  i=0;
//	  	  }
	  HAL_Delay(5);
	  if (start == 1) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
	  else HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);



	  if(now_time - uart_tick >= 1000)
	  {
		  /*Gửi dữ liệu lên giao diện app, dùng Hal_Systick*/
		  sprintf(buffer,"Data:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
				  "%d,%d,%d,%d,%d,%d,%d.\n",
				  bms1.cells[0].voltage, bms1.cells[1].voltage, bms1.cells[2].voltage,
				  bms2.cells[0].voltage, bms2.cells[1].voltage, bms2.cells[2].voltage,
				  bms1.cells[0].current, bms1.cells[1].current, bms1.cells[2].current,
				  bms2.cells[0].current, bms2.cells[1].current, bms2.cells[2].current,
				  bms1.cells[0].Q_mAh, bms1.cells[1].Q_mAh, bms1.cells[2].Q_mAh,
				  bms2.cells[0].Q_mAh, bms2.cells[1].Q_mAh, bms2.cells[2].Q_mAh,
				  bms1.cells[0].soc, bms1.cells[1].soc, bms1.cells[2].soc,
				  bms2.cells[0].soc, bms2.cells[1].soc, bms2.cells[2].soc,
				  bms1.cells[0].temp, bms1.cells[1].temp, bms1.cells[2].temp,
				  bms2.cells[0].temp, bms2.cells[1].temp, bms2.cells[2].temp,
				  current, start);
		  HAL_UART_Transmit(&huart1, (uint8_t *)buffer, strlen(buffer),10000);

		  uart_tick = now_time;
	  }

	  if(now_time - can_tick >= 5000)
	  	  {
	  		  /*Xử lý dữ liệu*/
	  		  if((bms2.rx_complete == 0x07)&&(bms1.rx_complete == 0x07))
	  		  {
	  		      proccess_data();
	  		  }
	  		  /*Truy ?n dữ liệu cho các slave*/
	  		  SendCellData();
	  		  can_tick = now_time;
	  	  }
//	  	  current = INA219_ReadCurrent(&hi2c1);
//	  	  current = I_Filter(current);
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
  hadc1.Init.NbrOfConversion = 2;
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
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = DISABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  CAN_FilterTypeDef FilterConfig;

  FilterConfig.FilterBank = 0;
  FilterConfig.FilterActivation = CAN_FILTER_ENABLE;
  FilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  FilterConfig.FilterIdHigh = (0x30 << 5);
  FilterConfig.FilterIdLow = (0x31 << 5);
  FilterConfig.FilterMaskIdHigh = (0x20 << 5);
  FilterConfig.FilterMaskIdLow = (0x21 << 5);
  FilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;
  FilterConfig.FilterScale = CAN_FILTERSCALE_16BIT;
  HAL_CAN_ConfigFilter(&hcan, &FilterConfig);


  FilterConfig.FilterBank = 1;
  FilterConfig.FilterActivation = CAN_FILTER_ENABLE;
  FilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  FilterConfig.FilterIdHigh = (0x32 << 5);
  FilterConfig.FilterIdLow = (0x7FF << 5);
  FilterConfig.FilterMaskIdHigh = (0x22 << 5);
  FilterConfig.FilterMaskIdLow = (0x7FF << 5);
  FilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;
  FilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  HAL_CAN_ConfigFilter(&hcan, &FilterConfig);

  /* USER CODE END CAN_Init 2 */

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
  htim2.Init.Period = 99;
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
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PB10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
