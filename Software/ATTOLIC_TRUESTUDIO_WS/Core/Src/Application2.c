/*
 * Application.c
 *
 *  Created on: Nov 1, 2019
 */

#include <Application.h>
#ifdef LCDTFT
extern volatile GUI_TIMER_TIME OS_TimeMS;
#endif
#ifdef HD44780
uint32_t OS_TimeMS;
#endif
//Uart vasiables
char UartRxData[100];
char UartTxData[100];
//
uint8_t Index = 0;
uint16_t SetPoint;
uint16_t SetPointBackup;
bool EncoderChanged=false;
uint8_t ChangedEncoderValueOnScreen=0;
//Temperature measurement varables
float ADCData = 0;
float T_tc = 0;
float T_amb = 23;
float U_measured;
const float U_seebeck = 26.2;
const float VoltageMultiplier = 3.662; //33000000/(4096*220)=3.662
uint16_t MovingAverage_T_tc = 0;
uint16_t Points = 0;
bool OutputState = false;
//PID variables
uint8_t OutputDuty = 10;
uint8_t OutputDutyFiltered = 0;
float Ts = 0.11;
uint8_t N = 15;
float Kp = 0;
float Ki = 0;
float Kd = 0;
float E0 = 0;
float E1 = 0;
float E2 = 0;
float U0 = 0;
float U1 = 0;
float U2 = 0;
float a0;
float a1;
float a2;
float b0;
float b1;
float b2;
float A1;
float A2;
float B0;
float B1;
float B2;
//
//const uint16_t EncoderOffset = 0x7FFF;
//state machine variables
bool SolderingIronIsInHolder;//1 if soldering iron is in the Holder
bool SolderingIronNotConnected;//1 if soldering iron unconnected
#ifdef LCDTFT
bool SolderingTipIsRemoved=false;//new PCB version can distinguish removed tip and unconnected soldering iron
extern WM_HWIN hDialog;//
extern WM_HWIN hText_0;//Set Temperature
extern WM_HWIN hText_1;//setpoint
extern WM_HWIN hText_2;//�C
extern WM_HWIN hText_3;//Soldering Iron Temperature
extern WM_HWIN hText_4;//actual temp
extern WM_HWIN hText_5;//�C
extern WM_HWIN hText_6;//Heating power
extern WM_HWIN hProgbar_0;//progress bar
#endif
//Flash variables
bool FlashWriteEnabled=true;
uint16_t VirtAddVarTab;//[NB_OF_VAR] = {0x0001};
//
uint16_t Counter = 0;
uint8_t Cnt2=0;
bool CounterFlag = false;
//defines
#define BlinkingPeriod 750//ms period time of blinking texts
#define ChangedEncoderValueOnScreenPeriod 4//4*BlinkingPeriod
#define TemperatureMovingAverageCoeff1 60//must be between 0 and 1
#define TemperatureMovingAverageCoeff2 (100-TemperatureMovingAverageCoeff1)
#define OutputDutyFilterCoeff1 15//must be between 0 and 1
#define OutputDutyFilterCoeff2 (100-OutputDutyFilterCoeff1)
#define EncoderOffset 0x7FFF
//
//#define SendMeasurementsTimer
#ifdef SendMeasurementsTimer
#define SendMeasurementsPeriod 110//ms
#endif
/* USER CODE END PV */
//
#ifdef LCDTFT
extern void Init_GUI(void);
#endif
//
// Converts a floating point number to string.
//float to char array conversion
void ftoa(float n, char *res, int afterpoint) {
	// Extract integer part
	int ipart = (int) n;
	// Extract floating part
	float fpart = n - (float) ipart;

	if (fpart < 0 || ipart < 0) {
		res[0] = '-';
		if (ipart < 0) {
			ipart *= -1;
		}
		if (fpart < 0) {
			fpart *= -1;
		}
		itoa(ipart, res + 1, 10);
	}
	// convert integer part to string
	else {
		itoa(ipart, res, 10);
	}
	int i = strlen(res);
	if (afterpoint != 0) {
		res[i] = '.';
		fpart = fpart * pow(10, afterpoint);
		itoa((int) fpart, res + i + 1, 10);
	}
}
//send measurements
void SendMeasurements(void) {
	char TmpBuffer[20];
	//Measuring Points
	itoa(Points, TmpBuffer, 10);
	strcpy(UartTxData, TmpBuffer);
	strcat(UartTxData, "; ");
	//ms value
	itoa(OS_TimeMS, TmpBuffer, 10);
	strcat(UartTxData, TmpBuffer);
	strcat(UartTxData, "; ");
	//Output 0 or 1
	itoa(OutputState, TmpBuffer, 10);
	strcat(UartTxData, TmpBuffer);
	strcat(UartTxData, "; ");
	//SetPoint temperature
	itoa(SetPoint, TmpBuffer, 10);
	strcat(UartTxData, TmpBuffer);
	strcat(UartTxData, "; ");
	//Tip temperature
	itoa(MovingAverage_T_tc, TmpBuffer, 10);
	strcat(UartTxData, TmpBuffer);
	strcat(UartTxData, "; ");
	//actual error signal
	ftoa(E0, TmpBuffer, 4);
	strcat(UartTxData, TmpBuffer);
	strcat(UartTxData, "; ");
	//Actual manupilator signal
	ftoa(U0, TmpBuffer, 4);
	strcat(UartTxData, TmpBuffer);
	strcat(UartTxData, "; ");
	//ftoa(U0,TmpBuffer,4);
	//output duty
	itoa(OutputDuty, TmpBuffer, 10);
	strcat(UartTxData, TmpBuffer);
	strcat(UartTxData, "\r\n");
	//send
	HAL_UART_Transmit(&huart2, (uint8_t*) UartTxData, strlen(UartTxData), 100);
}
//external interrupt
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == INT_ZC_Pin) {
		if (HAL_GPIO_ReadPin(INT_ZC_GPIO_Port, INT_ZC_Pin) == 1) { // if GPIO==0 falling edge after zero crossing
			if (Index == 0) { //under the first half-wave ADC measurement is performed
				//ACD+precision OPA
				//2 measures average
				ADCData = 0;				//zeroing the variable
				for (uint8_t i = 0; i < 3; i++) {
					HAL_GPIO_WritePin(INH_ADC_GPIO_Port, INH_ADC_Pin,GPIO_PIN_RESET); //Release the ADC input
					HAL_ADC_Start(&hadc1);
					if (HAL_ADC_PollForConversion(&hadc1, 1000) == HAL_OK) { //analog read
						ADCData += HAL_ADC_GetValue(&hadc1); //Add new adc value to the variable
					}
					HAL_GPIO_WritePin(INH_ADC_GPIO_Port, INH_ADC_Pin,GPIO_PIN_SET); //Pull down the the ADC input
					HAL_ADC_Stop(&hadc1); //stop the ADC module
				}
				ADCData /= 3; //average of the 2 samples
				if (ADCData > 3500) {
					SolderingTipIsRemoved = true;
					OutputState = false;
					OutputDuty = 0;
				} else {
					SolderingTipIsRemoved = false;
					OutputState = true;
					//convert to celsius
					U_measured = ADCData * VoltageMultiplier; // measured TC voltage in microvolts = Uadc(LSB) *3.662;
					T_tc = (U_measured / U_seebeck) + T_amb; //Termocoulpe temperature=Measured voltage/seebeck voltage+Ambient temperature (cold junction compensation)
					//T_tc=(uint16_t)T_tc;
					MovingAverage_T_tc = ((uint16_t)T_tc * TemperatureMovingAverageCoeff1 + MovingAverage_T_tc * TemperatureMovingAverageCoeff2)/ 100;//exponential filter with 2 sample and lambda=0.8
					MovingAverage_T_tc = ((MovingAverage_T_tc + 4) / 5) * 5;//rounding to 0 or 5 //MovingAverage_T_tc=T_tc;
				}
#ifdef	PID_CTRL
				//PID begin
				if (OutputState == true) {
					b0 = (Kp * (1 + N * Ts)) + (Ki * Ts * (1 + N * Ts))	+ (Kd * N);
					b1 = -((Kp * (2 + N * Ts)) + (Ki * Ts) + (2 * Kd * N));
					b2 = Kp + (Kd * N);
					a0 = 1 + N * Ts;
					a1 = -(2 + N * Ts);
					a2 = 1;
					A1 = a1 / a0;
					A2 = a2 / a0;
					B0 = b0 / a0;
					B1 = b1 / a0;
					B2 = b2 / a0;
					E2 = E1;
					E1 = E0;
					U2 = U1;
					U1 = U0;
					E0 = SetPoint - T_tc;//MovingAverage_T_tc;			//Actual error
					U0 = -(A1 * U1) - (A2 * U2) + (B0 * E0) + (B1 * E1)	+ (B2 * E2);
					if (E0 < 0) {
						E0 = 0;
						//U0 = 0;
					}
					if (U0 > 100) {
						U0 = 100;
					}
					if (U0 < 0) {
						U0 = 0;
					}
					OutputDuty = (((int16_t) U0 + 5) / 10) * 10;//rounding output duty up or down to tens 0,10,...,90,100
					OutputDutyFiltered=(OutputDutyFilterCoeff1*OutputDuty+OutputDutyFilterCoeff2*OutputDutyFiltered)/100;
				} else {
					OutputDuty = 0;
				}
				//PID end
#endif
#ifdef HYST_CTRL
				//do nothing
#endif
				//sending measurements
				SendMeasurements();
				Points++;
			}
			Index++;
			if (Index == 11) {
				Index = 0;
			}
		}
		if (HAL_GPIO_ReadPin(INT_ZC_GPIO_Port, INT_ZC_Pin) == 0) {// rising edge before zero crossing
			if (Index == 0) {
				HAL_GPIO_WritePin(HEATING_GPIO_Port, HEATING_Pin,GPIO_PIN_RESET); //output off
			}
			else {
				if (OutputState == true) {
#ifdef HYST_CTRL
					if (HAL_GPIO_ReadPin(HEATING_GPIO_Port, HEATING_Pin) == 1) { //Output=1
						if (MovingAverage_T_tc >= (SetPoint + 5)) {
							HAL_GPIO_WritePin(HEATING_GPIO_Port, HEATING_Pin,GPIO_PIN_RESET); //output off
						}
					}
					if (HAL_GPIO_ReadPin(HEATING_GPIO_Port, HEATING_Pin) == 0) { //Output=0
						if (MovingAverage_T_tc <= (SetPoint - 5)) {
							HAL_GPIO_WritePin(HEATING_GPIO_Port, HEATING_Pin,GPIO_PIN_SET); //output on
						}
					}
#endif
#ifdef PID_CTRL
					if (Index < (OutputDuty / 10) + 1) {
						HAL_GPIO_WritePin(HEATING_GPIO_Port, HEATING_Pin,GPIO_PIN_SET); //output on
					}
					else {
						HAL_GPIO_WritePin(HEATING_GPIO_Port, HEATING_Pin,GPIO_PIN_RESET); //output off
					}
#endif
				}
				else {
					HAL_GPIO_WritePin(HEATING_GPIO_Port, HEATING_Pin,GPIO_PIN_RESET); //output off
				}
			}
		}
	}
	if (GPIO_Pin == ENC_BUT_Pin) { //Encoder button
		if (HAL_GPIO_ReadPin(ENC_BUT_GPIO_Port, ENC_BUT_Pin) == 0) { // if GPIO==0 -> falling edge
		}
		if (HAL_GPIO_ReadPin(ENC_BUT_GPIO_Port, ENC_BUT_Pin) == 1) { // rising edge
			//store actual encoder value to flash
			if (FlashWriteEnabled) {
				ChangedEncoderValueOnScreen=ChangedEncoderValueOnScreenPeriod;
				uint16_t tmpWrite = SetPointBackup / 10;
				uint16_t tmpRead;
				if((EE_ReadVariable(0x0001,  &tmpRead)) != HAL_OK)
				{
					Error_Handler();
				}
				if (tmpRead != tmpWrite) {
					if((EE_WriteVariable(0x0001,  tmpWrite)) != HAL_OK)
					{
						Error_Handler();
					}
					if((EE_ReadVariable(0x0001,  &tmpRead)) != HAL_OK)
					{
						Error_Handler();
					}
					if (tmpWrite != tmpRead) {
						//flash write error
						asm("nop");//for debugging
					} else {
						//flash write ok
						asm("nop");//for debugging
					}
				}
			}
			else {
				asm("nop");//for debugging
			}
		}
	}
	if (GPIO_Pin == SLEEP_Pin) {			//Encoder button
	}
}
//system timer 1ms
void HAL_SYSTICK_Callback(void) {
	OS_TimeMS++;
	//
	Counter++;
	if (Counter == BlinkingPeriod) {
		if (CounterFlag) {
			CounterFlag = false;
		} else {
			CounterFlag = true;
		}
		if(ChangedEncoderValueOnScreen>0){
			ChangedEncoderValueOnScreen--;
		}
		Counter = 0;
	}
#ifdef SendMeasurementsTimer
	Cnt2++;
	if(Cnt2==SendMeasurementsPeriod){
		SendMeasurements();
		Cnt2=0;
	}
#endif
}
//Uart functions
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART2) {
		//START: Kp=1.00, Ki=2.00, Kd=0.00 :END
		//interpreting the incoming data
		if(		UartRxData[0]=='S' &&
				UartRxData[1]=='T' &&
				UartRxData[2]=='A' &&
				UartRxData[3]=='R' &&
				UartRxData[4]=='T' &&
				UartRxData[5]==':' &&
				UartRxData[6]==' ' &&
				//Kp
				UartRxData[7]=='K' &&
				UartRxData[8]=='p' &&
				UartRxData[9]=='=' &&
				(UartRxData[10]>='0' && UartRxData[10]<='9') &&
				UartRxData[11]=='.' &&
				(UartRxData[12]>='0' && UartRxData[12]<='9') &&
				(UartRxData[13]>='0' && UartRxData[13]<='9') &&
				UartRxData[14]==',' &&
				UartRxData[15]==' ' &&
				//Ki
				UartRxData[16]=='K' &&
				UartRxData[17]=='i' &&
				UartRxData[18]=='=' &&
				(UartRxData[19]>='0' && UartRxData[19]<='9') &&
				UartRxData[20]=='.' &&
				(UartRxData[21]>='0' && UartRxData[21]<='9') &&
				(UartRxData[22]>='0' && UartRxData[22]<='9') &&
				UartRxData[23]==',' &&
				UartRxData[24]==' ' &&
				//Kd
				UartRxData[25]=='K' &&
				UartRxData[26]=='d' &&
				UartRxData[27]=='=' &&
				(UartRxData[28]>='0' && UartRxData[28]<='9') &&
				UartRxData[29]=='.' &&
				(UartRxData[30]>='0' && UartRxData[30]<='9') &&
				(UartRxData[31]>='0' && UartRxData[31]<='9') &&
				UartRxData[32]==' ' &&
				//
				UartRxData[33]==':' &&
				UartRxData[34]=='E' &&
				UartRxData[35]=='N' &&
				UartRxData[36]=='D' &&
				UartRxData[37]=='\r' &&
				UartRxData[38]=='\n' )
		{
			Kp=(UartRxData[10]-'0')+(0.1*(UartRxData[12]-'0'))+(0.01*(UartRxData[13]-'0'));
			Ki=(UartRxData[19]-'0')+(0.1*(UartRxData[21]-'0'))+(0.01*(UartRxData[22]-'0'));
			Kd=(UartRxData[28]-'0')+(0.1*(UartRxData[30]-'0'))+(0.01*(UartRxData[31]-'0'));
			if((EE_WriteVariable(0x0002,  (uint16_t)(Kp*100))) != HAL_OK)
			{
				Error_Handler();
			}
			if((EE_WriteVariable(0x0003,  (uint16_t)(Ki*100))) != HAL_OK)
			{
				Error_Handler();
			}
			if((EE_WriteVariable(0x0004,  (uint16_t)(Kd*100))) != HAL_OK)
			{
				Error_Handler();
			}
		}
		else{
			HAL_UART_Transmit_IT(&huart2, (uint8_t*) "Wrong format\r\n", 12);
		}
		HAL_UART_Receive_IT(&huart2, (uint8_t*)UartRxData, 39);
	}
}
//
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->ErrorCode == HAL_UART_ERROR_ORE) {
		HAL_UART_Transmit_IT(&huart2, (uint8_t*) "FAIL\r\n", 6);
		HAL_UART_Receive_IT(&huart2, (uint8_t*)UartRxData, 39);
	}
}
//LCD functions
#ifdef HD44780
void LCD_text(const char *q) {
	while (*q) {
		LCD_write(*q++, 0xFF);
	}
}
void LCD_write(unsigned char c, unsigned char d) {
	if (d == 0x00) {
		HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_RESET);
	} else {
		HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_SET);
	}
	HAL_Delay(1);
	LCD_DATA_PORT->ODR &= 0xFFFFFF00;
	LCD_DATA_PORT->ODR |= c;
	HAL_GPIO_WritePin(LCD_E_GPIO_Port, LCD_E_Pin, GPIO_PIN_SET);
	asm("nop");
	HAL_GPIO_WritePin(LCD_E_GPIO_Port, LCD_E_Pin, GPIO_PIN_RESET);
}
void LCD_init(void) {
	user_pwm_setvalue(100);
	HAL_GPIO_WritePin(GPIOB, LCD_E_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LCD_RW_GPIO_Port, LCD_RW_Pin, GPIO_PIN_RESET);

	HAL_Delay(100);
	LCD_write(0x38, 0x00);
	HAL_Delay(1);
	LCD_write(0x38, 0x00);
	HAL_Delay(1);
	LCD_write(0x38, 0x00);
	LCD_write(0x38, 0x00);
	LCD_write(0x38, 0x00);
	LCD_write(0x0C, 0x00); // Make cursorinvisible
	LCD_write(0x01, 0x00);
	HAL_Delay(2);
	LCD_write(0x6, 0x00); // Set entry Mode(auto increment of cursor)

	LCD_write(0x01, 0x00);
	HAL_Delay(2);
	LCD_write(0x80, 0x00);
	LCD_text("Set Temp:");
	LCD_write(0x8E, 0x00);
	LCD_text("C");
	LCD_write(0xC0, 0x00);
	LCD_text("Iron Temp:");
	LCD_write(0xD4, 0x00);
	LCD_text("");
	LCD_write(0x94, 0x00);
	LCD_text("");
}
//PWM LCD backlight
void user_pwm_setvalue(uint16_t value) {
	TIM_OC_InitTypeDef sConfigOC;

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = value;
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}
//
#endif
//
void StateMachine(void){
	//local varaibles
	char TmpStr[5];
	uint16_t EncoderReadValue;
	//
	if(HAL_GPIO_ReadPin(SLEEP_GPIO_Port,SLEEP_Pin)==1){
		SolderingIronIsInHolder=true;//Soldering iron is in the holder
	}
	else{
		SolderingIronIsInHolder=false;//Soldering iron is not in the holder
	}
#ifdef LCDTFT
	if(HAL_GPIO_ReadPin(SNC_GPIO_Port,SNC_Pin)==1){//soldering iron is connected
		SolderingIronNotConnected=false;
	}
	else{
		SolderingIronNotConnected=true;
	}
	//
	if(TIM2->CNT<0x8009){//if encoder less than 0x7FFF+0x000A=0x8009 - 100�C
		TIM2->CNT=0x8009;//low level saturation at 0x8009
	}
	if(TIM2->CNT>0x802C){//if encoder bigger than 0x7FFF+0x002D=0x802C - 450�C
		TIM2->CNT=0x802C;//top level saturation at 0x802C
	}
	EncoderReadValue=(TIM2->CNT-0x7FFF)*10;
	if(EncoderReadValue!=SetPointBackup){
		ChangedEncoderValueOnScreen=ChangedEncoderValueOnScreenPeriod;
	}
	SetPointBackup=EncoderReadValue;//setpoint is 10*Encoder data-EncoderOffset
	//
	if(SolderingTipIsRemoved==true||SolderingIronNotConnected==true){
		PROGBAR_SetValue(hProgbar_0, 0);//output duty
		if(CounterFlag){
			TEXT_SetText(hText_4, "0");
			if(SolderingTipIsRemoved==true){
				TEXT_SetText(hText_6, "Soldering tip is removed!");
			}
			if(SolderingIronNotConnected==true){
				TEXT_SetText(hText_6, "Soldering iron is not connected!");
			}
		}
		else{
			TEXT_SetText(hText_4, " ");
			TEXT_SetText(hText_6, " ");
		}
		//
	    TEXT_SetText(hText_0, "Soldering\n Temperature");
		sprintf(TmpStr,"%u",SetPointBackup);
		TEXT_SetText(hText_2, TmpStr);
		SetPoint=SetPointBackup;
	}
	else{
		TEXT_SetText(hText_6, "Heating Power");
		PROGBAR_SetValue(hProgbar_0, OutputDutyFiltered);//output duty
		sprintf(TmpStr,"%u",MovingAverage_T_tc);//Soldering iron tip temperature
		TEXT_SetText(hText_4, TmpStr);
		//
		if(SolderingIronIsInHolder==true){
			if(SetPointBackup>150){
				SetPoint=150;
			}
			else{
				SetPoint=SetPointBackup;
			}
			if(ChangedEncoderValueOnScreen>0){
			    TEXT_SetText(hText_0, "Soldering\n Temperature");
				sprintf(TmpStr,"%u",SetPointBackup);
				TEXT_SetText(hText_2, TmpStr);
			}
			else{
				if(CounterFlag){
				    TEXT_SetText(hText_0, "Soldering\n Temperature");
					sprintf(TmpStr,"%u",SetPointBackup);
					TEXT_SetText(hText_2, TmpStr);
				}
				else{
				    TEXT_SetText(hText_0, "Sleep\n Temperature");
				    sprintf(TmpStr,"%u",SetPoint);
				    TEXT_SetText(hText_2, TmpStr);//sleep temperature
				}
			}
		}
		else{
		    TEXT_SetText(hText_0, "Soldering\n Temperature");
			sprintf(TmpStr,"%u",SetPointBackup);
			TEXT_SetText(hText_2, TmpStr);
			SetPoint=SetPointBackup;
		}
	}
#endif
#ifdef HD44780
	uint8_t TmpBuf[40];
	if (TIM2->CNT < 0x8009) {
		TIM2->CNT = 0x8009;
	}
	if (TIM2->CNT > 0x802C) {
		TIM2->CNT = 0x802C;
	}
	SetPoint = (TIM2->CNT - EncoderOffset) * 10;
	//setpoint
	uint16_t temp = SetPoint;
	uint8_t i = 0;
	TmpBuf[i] = (temp / 100) + 0x30;		//százas
	if (TmpBuf[i] != '0') {
		i++;
	}
	temp %= 100;
	TmpBuf[i++] = (temp / 10) + 0x30;		//tizes
	temp %= 10;
	TmpBuf[i++] = temp + 0x30;		//egyes
	TmpBuf[i++] = ' ';
	TmpBuf[i++] = 0xDF;		//Celsius fok
	TmpBuf[i++] = 'C';
	TmpBuf[i++] = ' ';
	TmpBuf[i++] = '\0';
	LCD_write(0x8A, 0x00);		//LCD első sor
	LCD_text((const char*) TmpBuf);

	//actual temperature
	if (SolderingTipIsRemoved) {
		temp = 0;
	} else {
		temp = MovingAverage_T_tc;
	}
	i = 0;
	if(temp==0){
		TmpBuf[i++]='0';
	}
	else{
		TmpBuf[i] = (temp / 100) + 0x30;		//százas
		if (TmpBuf[i] != '0') {
			i++;
		}
		temp %= 100;
		TmpBuf[i++] = (temp / 10) + 0x30;		//tizes
		temp %= 10;
		TmpBuf[i++] = temp + 0x30;		//egyes
	}
	TmpBuf[i++] = ' ';
	TmpBuf[i++] = 0xDF;		//Celsius fok
	TmpBuf[i++] = 'C';
	TmpBuf[i++] = ' ';
	TmpBuf[i++] = ' ';
	TmpBuf[i++] = '\0';
	LCD_write(0xCB, 0x00);		//
	LCD_text((const char*) TmpBuf);
	//Sleep
	if (SolderingTipIsRemoved==1) {
		FlashWriteEnabled=false;
		LCD_write(0x94, 0x00);
		LCD_text("Iron Is Unconnected!");
	} else if (SolderingIronIsInHolder==1) {
		FlashWriteEnabled=false;
		LCD_write(0x94, 0x00);
		LCD_text("Sleep temp: 150");
		LCD_write(0xDF, 0xFF);		//Celsius fok
		LCD_write('C', 0xFF);
		SetPointBackup = SetPoint;
		if (SetPoint > 150) {
			SetPoint = 150;
		}
	} else {
		FlashWriteEnabled=true;
		LCD_write(0x94, 0x00);
		LCD_text("                    ");
		//SetPoint = SetPointBackup;
	}
	if (OutputState) {
		LCD_write(0xD4, 0x00);
		LCD_text("Output: ON ");
	} else {
		LCD_write(0xD4, 0x00);
		LCD_text("Output: OFF");
	}
#endif
}
//
void MainInit(void) {
	uint16_t tmp = 0;
	HAL_TIM_Encoder_Start(&htim2,TIM_CHANNEL_ALL);//encoder timer2
#ifdef LCDTFT
	Init_GUI();//initializing graphics
#endif
#ifdef HD44780
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);//backlight PWM timer
	user_pwm_setvalue(100);//set pwm max
	LCD_init();//init HD44780 LCD
#endif
	//read stored temperature value from flash
	HAL_FLASH_Unlock();
	if (EE_Init() != EE_OK) {
		Error_Handler();
	}
	if ((EE_ReadVariable(0x0001, &tmp)) == HAL_OK) {
		TIM2->CNT = (uint8_t) tmp + EncoderOffset;
	}
	else{
		TIM2->CNT = 10 + EncoderOffset;//safety default 100�C
	}
	if ((EE_ReadVariable(0x0002, &tmp)) == HAL_OK) {//kp
		Kp=tmp;
		Kp/=100;
	}
	else{
		Kp=0;
	}
	//
	if ((EE_ReadVariable(0x0003, &tmp)) == HAL_OK) {//ki
		Ki=tmp;
		Ki/=100;
	}
	else{
		Ki=0;
	}
	//
	if ((EE_ReadVariable(0x0004, &tmp)) == HAL_OK) {//kd
		Kd=tmp;
		Kd/=100;
	}
	else{
		Kd=0;
	}
	//
	HAL_UART_Receive_IT(&huart2, (uint8_t*)UartRxData, 39);
	SetPointBackup=(TIM2->CNT-0x7FFF)*10;
}
//
void MainTask(void) {
	while (1) {
#ifdef LCDTFT
		StateMachine();//
		GUI_Exec();//gui execution
		GUI_Delay(50);//gui sleep 50ms
#endif
#ifdef HD44780
		StateMachine();
		HAL_Delay(200);
#endif
	}
}
//end
