//---------- Includes ----------------------------------------------------------
#include "SHT2x.h"


#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "I2C_HAL.h"
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
unsigned char SHT2x_CheckCrc(unsigned char data[], unsigned char nbrOfBytes, unsigned char checksum)
{
  unsigned char crc = 0;	
  unsigned char byteCtr;
  //calculates 8-Bit checksum with given polynomial
  for (byteCtr = 0; byteCtr < nbrOfBytes; ++byteCtr)
  { crc ^= (data[byteCtr]);
    for (unsigned char bit = 8; bit > 0; --bit)
    { if (crc & 0x80) crc = (crc << 1) ^ POLYNOMIAL;
      else crc = (crc << 1);
    }
  }
  if (crc != checksum) return CHECKSUM_ERROR;
  else return 0;
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
unsigned char SHT2x_ReadUserRegister(unsigned char *pRegisterValue)
{
  unsigned char checksum;   //variable for checksum byte
  unsigned char error=0;    //variable for error code

  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_W);
  error |= I2c_WriteByte (USER_REG_R);
  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_R);
  *pRegisterValue = I2c_ReadByte(ACK);
  checksum=I2c_ReadByte(NO_ACK);
  error |= SHT2x_CheckCrc (pRegisterValue,1,checksum);
  I2c_StopCondition();
  return error;
}

unsigned char SHT2x_ReadTemperatureHumidity(float *temperature,
                                            float *humidity)
{
    unsigned int rawTemperature = 0;
    unsigned int rawHumidity = 0;

    unsigned char error;

    if ((temperature == NULL) || (humidity == NULL))
        return TIME_OUT_ERROR;

    /* Read temperature */
    error = SHT2x_MeasureHM(TEMP, &rawTemperature);

    if (error != 0)
    {
        printf("SHT20 Temperature Read Error: %d\n", error);
        return error;
    }

    /* Read humidity */
    error = SHT2x_MeasureHM(HUMIDITY, &rawHumidity);

    if (error != 0)
    {
        printf("SHT20 Humidity Read Error: %d\n", error);
        return error;
    }

    /* Convert raw values into actual readings */
    *temperature = SHT2x_CalcTemperatureC(rawTemperature);
    *humidity = SHT2x_CalcRH(rawHumidity);

    /* Limit humidity to valid range */
    if (*humidity > 100.0f)
        *humidity = 100.0f;

    if (*humidity < 0.0f)
        *humidity = 0.0f;

    return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
unsigned char SHT2x_WriteUserRegister(unsigned char *pRegisterValue)
{
  unsigned char error=0;   //variable for error code

  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_W);
  error |= I2c_WriteByte (USER_REG_W);
  error |= I2c_WriteByte (*pRegisterValue);
  I2c_StopCondition();
  return error;
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
// unsigned char SHT2x_MeasureHM(etSHT2xMeasureType eSHT2xMeasureType, unsigned int *pMeasurand)
// {
//   unsigned char  checksum;   //checksum
//   unsigned char  data[2];    //data array for checksum verification
//   unsigned char  error=0;    //error variable
//   unsigned int i;          //counting variable
//   float           result;
//   //-- write I2C sensor address and command --
//   I2c_StartCondition();
//   error |= I2c_WriteByte (I2C_ADR_W); // I2C Adr
//   switch(eSHT2xMeasureType)
//   { case HUMIDITY: error |= I2c_WriteByte (TRIG_RH_MEASUREMENT_HM); break;
//     case TEMP    : error |= I2c_WriteByte (TRIG_T_MEASUREMENT_HM);  break;
//     default: assert(0);
//   }
//   //-- wait until hold master is released --
//   I2c_StartCondition();
//   error |= I2c_WriteByte (I2C_ADR_R);
//   gpio_set_direction  (SCL, GPIO_MODE_INPUT);                                                             // Set the GPIO as a push/pull output 
//   for(i=0; i<1000; i++)                                                                                   // wait until master hold is released or
//   { 
// 		vTaskDelay(20 / portTICK_PERIOD_MS);															                                      // Generate the delay
//     if (gpio_get_level(SCL)==1) 
//       {
//     	  //printf("Measurement Done: %d\r\n", i);
//         break;
//       }
//   }
//   //-- check for timeout --
//   if(gpio_get_level(SCL)==0) 
//     {
//       error |= TIME_OUT_ERROR;
//     	printf("TimeOut While Measurement: %d\r\n", error);
//     }
//   gpio_set_direction  (SCL, GPIO_MODE_OUTPUT);                                                            // Set the GPIO as a push/pull output 
//   data[0] = I2c_ReadByte(ACK);                                                                            //-- read two data bytes and one checksum byte --  
//   data[1] = I2c_ReadByte(ACK);
//   checksum=I2c_ReadByte(NO_ACK);
//   printf("Measurement H[%2x] L[%2x] C[%2x]    ",data[0],data[1],checksum);
//   if(eSHT2xMeasureType == TEMP)
//     {                                                                                                     // 
//       i = data[0];                                                                                        // 
//       i = i * data[1];
//       result = SHT2x_CalcTemperatureC(i);                                                                 // 
//       printf("Temperature: %2f\r\n",result);                                                              // 
//     }                                                                                                     // 
//   else                                                                                                    // If it is humidity
//     {                                                                                                     // 
//       i = data[0];                                                                                        // 
//       i = i * data[1];
//       result = SHT2x_CalcRH(i);                                                                           // 
//       printf("Humidity: %2f\r\n",result);                                                              // 
//     }                                                                                                     // 
//   //-- verify checksum --
//   error |= SHT2x_CheckCrc (data,2,checksum);
//   I2c_StopCondition();
//   return error;
// }


unsigned char SHT2x_MeasureHM(etSHT2xMeasureType eSHT2xMeasureType,
                              unsigned int *pMeasurand)
{
    unsigned char checksum;
    unsigned char data[2];
    unsigned char error = 0;
    unsigned int i;
    float result;

    // Start measurement command
    I2c_StartCondition();

    error |= I2c_WriteByte(I2C_ADR_W);

    switch (eSHT2xMeasureType)
    {
        case HUMIDITY:
            error |= I2c_WriteByte(TRIG_RH_MEASUREMENT_HM);
            break;

        case TEMP:
            error |= I2c_WriteByte(TRIG_T_MEASUREMENT_HM);
            break;

        default:
            I2c_StopCondition();
            return TIME_OUT_ERROR;
    }

    // Wait for measurement completion (clock stretching)
    I2c_StartCondition();
    error |= I2c_WriteByte(I2C_ADR_R);

    gpio_set_direction(SCL, GPIO_MODE_INPUT);

    for (i = 0; i < 1000; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1));

        if (gpio_get_level(SCL) == 1)
            break;
    }

    if (gpio_get_level(SCL) == 0)
    {
        error |= TIME_OUT_ERROR;
        gpio_set_direction(SCL, GPIO_MODE_OUTPUT);
        I2c_StopCondition();
        return error;
    }

    gpio_set_direction(SCL, GPIO_MODE_OUTPUT);

    // Read measurement data
    data[0] = I2c_ReadByte(ACK);
    data[1] = I2c_ReadByte(ACK);
    checksum = I2c_ReadByte(NO_ACK);

    // Combine MSB and LSB into 16-bit measurement
    i = ((unsigned int)data[0] << 8) | data[1];

    // Verify CRC before using measurement
    error |= SHT2x_CheckCrc(data, 2, checksum);

    I2c_StopCondition();

    if (error != 0)
        return error;

    // Store raw measurement
    *pMeasurand = i;

    // Convert measurement
    if (eSHT2xMeasureType == TEMP)
    {
        result = SHT2x_CalcTemperatureC(i);
        printf("Temperature: %.2f C\r\n", result);
    }
    else
    {
        result = SHT2x_CalcRH(i);
        printf("Humidity: %.2f %%\r\n", result);
    }

    return error;
}

//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
/*
unsigned char SHT2x_MeasurePoll(etSHT2xMeasureType eSHT2xMeasureType, unsigned int *pMeasurand)
{
  unsigned char  checksum;   //checksum
  unsigned char  data[2];    //data array for checksum verification
  unsigned char  error=0;    //error variable
  unsigned int i=0;        //counting variable

  //-- write I2C sensor address and command --
  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_W); // I2C Adr
  switch(eSHT2xMeasureType)
  { case HUMIDITY: error |= I2c_WriteByte (TRIG_RH_MEASUREMENT_POLL); break;
    case TEMP    : error |= I2c_WriteByte (TRIG_T_MEASUREMENT_POLL);  break;
    default: assert(0);
  }
  //-- poll every 10ms for measurement ready. Timeout after 20 retries (200ms)--
  do
  { I2c_StartCondition();
		vTaskDelay(10 / portTICK_PERIOD_MS);															                                    // Generate the delay
    if(i++ >= 20) break;
  } while(I2c_WriteByte (I2C_ADR_R) == ACK_ERROR);
  if (i>=20) error |= TIME_OUT_ERROR;

  //-- read two data bytes and one checksum byte --
  pMeasurand->s16.u8H = data[0] = I2c_ReadByte(ACK);
  pMeasurand->s16.u8L = data[1] = I2c_ReadByte(ACK);
  checksum=I2c_ReadByte(NO_ACK);

  //-- verify checksum --
  error |= SHT2x_CheckCrc (data,2,checksum);
  I2c_StopCondition();

  return error;
}
*/
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
unsigned char SHT2x_SoftReset()
{
  unsigned char  error=0;           //error variable
  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_W); // I2C Adr
  error |= I2c_WriteByte (SOFT_RESET);                            // Command
  I2c_StopCondition();
	vTaskDelay(15 / portTICK_PERIOD_MS);															                                    // Generate the delay
  return error;
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
float SHT2x_CalcRH(unsigned int u16sRH)
{
  float humidityRH;              // variable for result
  u16sRH &= ~0x0003;          // clear bits [1..0] (status bits)
  //-- calculate relative humidity [%RH] --
  humidityRH = -6.0 + 125.0/65536 * (float)u16sRH; // RH= -6 + 125 * SRH/2^16
  return humidityRH;
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
float SHT2x_CalcTemperatureC(unsigned int u16sT)
{
  float temperatureC;            // variable for result
  u16sT &= ~0x0003;           // clear bits [1..0] (status bits)
  //-- calculate temperature [�C] --
  temperatureC= -46.85 + 175.72/65536 *(float)u16sT; //T= -46.85 + 175.72 * ST/2^16
  return temperatureC;
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
unsigned char SHT2x_GetSerialNumber(unsigned char u8SerialNumber[])
{
  unsigned char  error=0;                          //error variable

  //Read from memory location 1
  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_W);    //I2C address
  error |= I2c_WriteByte (0xFA);         //Command for readout on-chip memory
  error |= I2c_WriteByte (0x0F);         //on-chip memory address
  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_R);    //I2C address
  u8SerialNumber[5] = I2c_ReadByte(ACK); //Read SNB_3
  I2c_ReadByte(ACK);                     //Read CRC SNB_3 (CRC is not analyzed)
  u8SerialNumber[4] = I2c_ReadByte(ACK); //Read SNB_2
  I2c_ReadByte(ACK);                     //Read CRC SNB_2 (CRC is not analyzed)
  u8SerialNumber[3] = I2c_ReadByte(ACK); //Read SNB_1
  I2c_ReadByte(ACK);                     //Read CRC SNB_1 (CRC is not analyzed)
  u8SerialNumber[2] = I2c_ReadByte(ACK); //Read SNB_0
  I2c_ReadByte(NO_ACK);                  //Read CRC SNB_0 (CRC is not analyzed)
  I2c_StopCondition();

  //Read from memory location 2
  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_W);    //I2C address
  error |= I2c_WriteByte (0xFC);         //Command for readout on-chip memory
  error |= I2c_WriteByte (0xC9);         //on-chip memory address
  I2c_StartCondition();
  error |= I2c_WriteByte (I2C_ADR_R);    //I2C address
  u8SerialNumber[1] = I2c_ReadByte(ACK); //Read SNC_1
  u8SerialNumber[0] = I2c_ReadByte(ACK); //Read SNC_0
  I2c_ReadByte(ACK);                     //Read CRC SNC0/1 (CRC is not analyzed)
  u8SerialNumber[7] = I2c_ReadByte(ACK); //Read SNA_1
  u8SerialNumber[6] = I2c_ReadByte(ACK); //Read SNA_0
  I2c_ReadByte(NO_ACK);                  //Read CRC SNA0/1 (CRC is not analyzed)
  I2c_StopCondition();

  return error;
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------