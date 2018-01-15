#include "dht22.h"

#include <stdbool.h>

typedef enum
{
	Falling,
	Rising
} OneWireSignalState;

uint16_t dht22_values[42*2];
uint8_t dht22_i_values;
OneWireSignalState dht22_firstSignalState;
OneWireSignalState dht22_prevSignalState;
DHT22_Error dht22_error;

#define TIM_ACTIVE_CHANNEL_FALLING HAL_TIM_ACTIVE_CHANNEL_1
#define TIM_CHANNEL_FALLING TIM_CHANNEL_1
#define TIM_ACTIVE_CHANNEL_RAISING HAL_TIM_ACTIVE_CHANNEL_2
#define TIM_CHANNEL_RISING TIM_CHANNEL_2

void SetGPIOMode(DHT22_Instance *instance, int isOut)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	GPIO_InitStruct.Pin = instance->TimerPin;

	if (isOut)
	{
		/*Configure GPIO pin Output Level */
		// ????????????????
		HAL_GPIO_WritePin(TimerCapture_GPIO_Port, TimerCapture_Pin, GPIO_PIN_SET);

		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
#ifdef STM32F1
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
#else
		GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
#endif
	}
	else
	{
#ifdef STM32F1
		//GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
		GPIO_InitStruct.Mode  = GPIO_MODE_AF_INPUT;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
#else
		GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
		GPIO_InitStruct.Speed     = GPIO_SPEED_HIGH;
		//GPIO_InitStruct.Alternate = handle->config.gpio_alternate_function;
#endif
		GPIO_InitStruct.Pull = GPIO_NOPULL; // GPIO_PULLUP
	}

	HAL_GPIO_Init(instance->TimerPort, &GPIO_InitStruct);
}

void DHT22_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if (dht22_i_values >= 42*2 && dht22_error == None)
		dht22_error = CountOverflow;
	if (dht22_error != None)
		return;

	uint32_t timChannelNumber;
	OneWireSignalState signalState;
	switch (htim->Channel)
	{
	case TIM_ACTIVE_CHANNEL_FALLING:
		timChannelNumber = TIM_CHANNEL_FALLING;
		signalState = Falling;
		break;
	case TIM_ACTIVE_CHANNEL_RAISING:
		timChannelNumber = TIM_CHANNEL_RISING;
		signalState = Rising;
		break;
	default:
		return;
	}

	if (dht22_i_values == 0)
		dht22_firstSignalState = dht22_prevSignalState = signalState;
	else
	{
		if (dht22_prevSignalState == signalState)
		{
			dht22_error = InterleaveMismatch;
			return;
		}
		dht22_prevSignalState = signalState;
	}

	dht22_values[dht22_i_values++] = HAL_TIM_ReadCapturedValue(htim, timChannelNumber);
}

#define DHT22_StartingLowLengthMin (80-5)
#define DHT22_StartingLowLengthMax (80+5)
#define DHT22_StartingHighLengthMin (80-5)
#define DHT22_StartingHighLengthMax (80+5)

#define DHT22_LowInterBitLengthMin (50-5)
#define DHT22_LowInterBitLengthMax (50+5)

#define DHT22_HighZeroBitLengthMin (26-2)
#define DHT22_HighZeroBitLengthMax (28+2)

#define DHT22_HighOneBitLengthMin (70-5)
#define DHT22_HighOneBitLengthMax (70+5)

#define DHT22_LowInterByteLengthMin (67-5)
#define DHT22_LowInterByteLengthMax (67+5)

bool DHT22_ReadInterBitSpace(uint8_t *i)
{
	uint16_t val = dht22_values[(*i)++];
	return (DHT22_LowInterBitLengthMin <= val && val <= DHT22_LowInterBitLengthMax);
}

bool DHT22_ReadInterByteSpace(uint8_t *i)
{
	uint16_t val = dht22_values[(*i)++];
	return (DHT22_LowInterByteLengthMin <= val && val <= DHT22_LowInterByteLengthMax);
}

int16_t DHT22_ReadOneByte(uint8_t *i)
{
	int16_t value = 0;
	int8_t j;
	for (j = 7; j >= 0; --j)
	{
		if (j != 7 && !DHT22_ReadInterBitSpace(i))
			return (int16_t)-1;
		uint16_t val = dht22_values[(*i)++];
		if (DHT22_HighZeroBitLengthMin <= val && val <= DHT22_HighZeroBitLengthMax)
		{
			// Bit "Zero"
		}
		else if (DHT22_HighOneBitLengthMin <= val && val <= DHT22_HighOneBitLengthMax)
		{
			// Bit "One"
			value |= 1 << j;
		}
		else
		{
			// Error
			return (int16_t)-1;
		}
	}
	return value;
}

int32_t DHT22_ReadOneValue(uint8_t *i)
{
	int16_t highByte = DHT22_ReadOneByte(i);
	if (highByte < 0)
		return (int32_t)-1;

	if (!DHT22_ReadInterByteSpace(i))
		return (int32_t)-1;

	int16_t lowByte = DHT22_ReadOneByte(i);
	if (lowByte < 0)
		return (int32_t)-1;

	return ((highByte&0xFF) << 8) + (lowByte & 0xFF);
}

DHT22_Value DHT22_GetValue(DHT22_Instance *instance)
{
	uint8_t i;
	DHT22_Value result;

	SetGPIOMode(instance, 1);
	HAL_GPIO_WritePin(TimerCapture_GPIO_Port, TimerCapture_Pin, GPIO_PIN_RESET);
	HAL_Delay(2);
	SetGPIOMode(instance, 0);
	__HAL_TIM_SET_COUNTER(instance->Timer, 0);
	dht22_i_values = 0;
	dht22_error = None;
	HAL_TIM_IC_Start_IT(instance->Timer, instance->TimerCaptureFallingEdgeChannel);
	HAL_TIM_IC_Start_IT(instance->Timer, instance->TimerCaptureRisingEdgeChannel);
	HAL_Delay(20);
	HAL_TIM_IC_Stop_IT(instance->Timer, instance->TimerCaptureFallingEdgeChannel);
	HAL_TIM_IC_Stop_IT(instance->Timer, instance->TimerCaptureRisingEdgeChannel);

	result.Error = StartSequenceMismatch;

	if (dht22_firstSignalState != Falling)
		return result;

	for (i = 42*2 - 1; i > 0; --i)
		dht22_values[i] = dht22_values[i] - dht22_values[i-1];

	if ((dht22_values[1] < DHT22_StartingLowLengthMin) || (dht22_values[1] > DHT22_StartingLowLengthMax) ||
		(dht22_values[2] < DHT22_StartingHighLengthMin) || (dht22_values[2] > DHT22_StartingHighLengthMax))
		return result;

	result.Error = DataTimingsMismatch;

	i = 3;
	if (!DHT22_ReadInterBitSpace(&i))
		return result;

	int32_t humidity = DHT22_ReadOneValue(&i);
	if (humidity < (int32_t)0)
		return result;
	result.Humidity = (uint16_t)humidity;

	if (!DHT22_ReadInterByteSpace(&i))
		return result;

	int32_t temperature = DHT22_ReadOneValue(&i);
	if (temperature < 0)
		return result;
	result.Temperature = (uint16_t)temperature;

	if (!DHT22_ReadInterByteSpace(&i))
		return result;

	int16_t crc_value = DHT22_ReadOneByte(&i);
	if (crc_value < 0)
		return result;

	uint32_t crc_calculated = (result.Humidity >> 8) + (result.Humidity & 0xFF) + (result.Temperature >> 8) + (result.Temperature & 0xFF);
	result.Error = ((crc_calculated & (uint32_t)0xFF) == crc_value) ? None : DataCrcMismatch;

	return result;
}
