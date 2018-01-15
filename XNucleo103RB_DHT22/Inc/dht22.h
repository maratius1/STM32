#ifndef DHT22_H
#define DHT22_H

#ifdef STM32F1
#include "stm32f1xx_hal.h"
#endif

#ifdef STM32F4
#include "stm32f4xx_hal.h"
#endif

#include <stdint.h>

typedef enum
{
	None, // No error
	InterleaveMismatch, // Falling/rising interleave error
	CountOverflow, // Signal falling/rising count overflow
	StartSequenceMismatch,
	DataTimingsMismatch,
	DataCrcMismatch
} DHT22_Error;

typedef struct
{
	DHT22_Error Error;
	uint16_t Humidity;
	uint16_t Temperature;
} DHT22_Value;

typedef struct
{
	TIM_HandleTypeDef *Timer;
	uint32_t TimerCaptureFallingEdgeChannel;
	uint32_t TimerCaptureRisingEdgeChannel;

	GPIO_TypeDef *TimerPort;
	uint16_t TimerPin;
} DHT22_Instance;

DHT22_Value DHT22_GetValue(DHT22_Instance *instance);
void DHT22_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim);

#endif /* DHT22_H */
