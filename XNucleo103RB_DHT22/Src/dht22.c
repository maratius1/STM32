#include "dht22.h"

#include <stdbool.h>

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

void DHT22_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim, DHT22_Instance *instance)
{
	if (instance->Internal.ReadValuesIndex >= 42*2 && instance->Internal.Error == None)
		instance->Internal.Error = CountOverflow;
	if (instance->Internal.Error != None)
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

	if (instance->Internal.ReadValuesIndex == 0)
		instance->Internal.FirstSignalState = instance->Internal.PrevSignalState = signalState;
	else
	{ 
		if (instance->Internal.PrevSignalState == signalState)
		{
			instance->Internal.Error = InterleaveMismatch;
			return;
		}
		instance->Internal.PrevSignalState = signalState;
	}

	instance->Internal.ReadValues[instance->Internal.ReadValuesIndex++] =
		HAL_TIM_ReadCapturedValue(htim, timChannelNumber);
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

bool DHT22_CheckStartSequence(DHT22_Internal *internal)
{
	internal->ReadValuesIndex = 3;
	uint16_t val = internal->ReadValues[1];
	if (val < DHT22_StartingLowLengthMin || DHT22_StartingLowLengthMax < val)
		return false;
	val = internal->ReadValues[2];
	return (DHT22_StartingHighLengthMin <= val && val <= DHT22_StartingHighLengthMax);
}

bool DHT22_ReadInterBitSpace(DHT22_Internal *internal)
{
	uint16_t val = internal->ReadValues[internal->ReadValuesIndex++];
	return (DHT22_LowInterBitLengthMin <= val && val <= DHT22_LowInterBitLengthMax);
}

bool DHT22_ReadInterByteSpace(DHT22_Internal *internal)
{
	uint16_t val = internal->ReadValues[internal->ReadValuesIndex++];
	return (DHT22_LowInterByteLengthMin <= val && val <= DHT22_LowInterByteLengthMax);
}

int16_t DHT22_ReadOneByte(DHT22_Internal *internal)
{
	int16_t value = 0;
	int8_t j;
	for (j = 7; j >= 0; --j)
	{
		if (j != 7 && !DHT22_ReadInterBitSpace(internal))
			return (int16_t)-1;
		uint16_t val = internal->ReadValues[internal->ReadValuesIndex++];
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

int32_t DHT22_ReadOneValue(DHT22_Internal *internal)
{
	int16_t highByte = DHT22_ReadOneByte(internal);
	if (highByte < 0)
		return (int32_t)-1;

	if (!DHT22_ReadInterByteSpace(internal))
		return (int32_t)-1;

	int16_t lowByte = DHT22_ReadOneByte(internal);
	if (lowByte < 0)
		return (int32_t)-1;

	return ((highByte&0xFF) << 8) + (lowByte & 0xFF);
}

DHT22_Value DHT22_GetValue(DHT22_Instance *instance)
{
	DHT22_Value result;

	SetGPIOMode(instance, 1);
	HAL_GPIO_WritePin(TimerCapture_GPIO_Port, TimerCapture_Pin, GPIO_PIN_RESET);
	HAL_Delay(2);
	SetGPIOMode(instance, 0);
	__HAL_TIM_SET_COUNTER(instance->Timer, 0);
	instance->Internal.ReadValuesIndex = 0;
	instance->Internal.Error = None;
	HAL_TIM_IC_Start_IT(instance->Timer, instance->TimerCaptureFallingEdgeChannel);
	HAL_TIM_IC_Start_IT(instance->Timer, instance->TimerCaptureRisingEdgeChannel);
	HAL_Delay(20);
	HAL_TIM_IC_Stop_IT(instance->Timer, instance->TimerCaptureFallingEdgeChannel);
	HAL_TIM_IC_Stop_IT(instance->Timer, instance->TimerCaptureRisingEdgeChannel);

	result.Error = StartSequenceMismatch;

	DHT22_Internal *internal = &(instance->Internal);

	if (internal->FirstSignalState != Falling)
		return result;

	for (int i = 42*2 - 1; i > 0; --i)
		internal->ReadValues[i] = internal->ReadValues[i] - internal->ReadValues[i-1];

	if (!DHT22_CheckStartSequence(internal))
		return result;

	result.Error = DataTimingsMismatch;

	if (!DHT22_ReadInterBitSpace(internal))
		return result;

	int32_t humidity = DHT22_ReadOneValue(internal);
	if (humidity < (int32_t)0)
		return result;
	result.Humidity = (uint16_t)humidity;

	if (!DHT22_ReadInterByteSpace(internal))
		return result;

	int32_t temperature = DHT22_ReadOneValue(internal);
	if (temperature < 0)
		return result;
	result.Temperature = (uint16_t)temperature;

	if (!DHT22_ReadInterByteSpace(internal))
		return result;

	int16_t crc_value = DHT22_ReadOneByte(internal);
	if (crc_value < 0)
		return result;

	uint32_t crc_calculated = (result.Humidity >> 8) + (result.Humidity & 0xFF) + (result.Temperature >> 8) + (result.Temperature & 0xFF);
	result.Error = ((crc_calculated & (uint32_t)0xFF) == crc_value) ? None : DataCrcMismatch;

	return result;
}
