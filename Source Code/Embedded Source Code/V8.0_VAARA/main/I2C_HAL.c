#include "I2C_HAL.h"
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "I2C_HAL.h"
void main_glob_delayI2C (unsigned long dly);                                                               // To generate the delay
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
void I2c_Init ()
{
    gpio_reset_pin      (SDA);                                                                            // Set the mode as gpio
    gpio_reset_pin      (SCL);                                                                            // Set the mode as gpio

    gpio_set_direction  (SDA, GPIO_MODE_OUTPUT);                                                          // Set the GPIO as a push/pull output 
    gpio_set_direction  (SCL, GPIO_MODE_OUTPUT);                                                          // Set the GPIO as a push/pull output 

    gpio_set_level      (SDA,1);                                                                          // I2C-bus idle mode SDA released (input)
    gpio_set_level      (SCL,1);                                                                          // I2C-bus idle mode SDA released (input)
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
void I2c_StartCondition ()
{
    gpio_set_direction  (SDA, GPIO_MODE_OUTPUT);                                                          // Set the GPIO as a push/pull output 
    gpio_set_direction  (SCL, GPIO_MODE_OUTPUT);                                                          // Set the GPIO as a push/pull output 

    main_glob_delayI2C  (50);
//  vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
    gpio_set_level      (SDA,1);                                                                          // I2C-bus idle mode SDA released (input)
    gpio_set_level      (SCL,1);                                                                          // I2C-bus idle mode SDA released (input)

    main_glob_delayI2C  (50);
//		vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
    gpio_set_level      (SDA,0);                                                                          // I2C-bus idle mode SDA released (input)

    main_glob_delayI2C  (50);
//		vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
    gpio_set_level      (SCL,0);                                                                          // I2C-bus idle mode SDA released (input)
    main_glob_delayI2C  (50);
//		vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
void I2c_StopCondition ()
{
    gpio_set_direction  (SDA, GPIO_MODE_OUTPUT);                                                          // Set the GPIO as a push/pull output 
    gpio_set_direction  (SCL, GPIO_MODE_OUTPUT);                                                          // Set the GPIO as a push/pull output 

    gpio_set_level      (SDA,0);                                                                          // I2C-bus idle mode SDA released (input)
    gpio_set_level      (SCL,0);                                                                          // I2C-bus idle mode SDA released (input)

    main_glob_delayI2C  (50);
//		vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
    gpio_set_level      (SCL,1);                                                                          // I2C-bus idle mode SDA released (input)

    main_glob_delayI2C  (50);
//		vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
    gpio_set_level      (SDA,1);                                                                          // I2C-bus idle mode SDA released (input)
    main_glob_delayI2C  (50);
//		vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
unsigned char I2c_WriteByte (unsigned char txByte)
{
  unsigned char mask,error=0;
  for (mask=0x80; mask>0; mask>>=1)                                                                       //shift bit for masking (8 times)
    { 
      gpio_set_level      (SCL,0);                                                                        // I2C-bus idle mode SDA released (input)
      main_glob_delayI2C  (50);
		  //vTaskDelay(1 / portTICK_PERIOD_MS);															                                    // Generate the delay
      if ((mask & txByte) == 0) 
        gpio_set_level      (SDA,0);                                                                      // I2C-bus idle mode SDA released (input)
      else 
        gpio_set_level      (SDA,1);                                                                      // I2C-bus idle mode SDA released (input)
      main_glob_delayI2C  (50);
		  //vTaskDelay(1 / portTICK_PERIOD_MS);															                                    // Generate the delay

      gpio_set_level      (SCL,1);                                                                        // I2C-bus idle mode SDA released (input)
      main_glob_delayI2C  (50);
		  //vTaskDelay(1 / portTICK_PERIOD_MS);															                                    // Generate the delay

      gpio_set_level      (SCL,0);                                                                        // I2C-bus idle mode SDA released (input)
      main_glob_delayI2C  (50);
		  //vTaskDelay(1 / portTICK_PERIOD_MS);															                                    // Generate the delay
    }
  gpio_set_level      (SDA,1);                                                                            // I2C-bus idle mode SDA released (input)
  gpio_set_direction  (SDA, GPIO_MODE_INPUT);                                                             // Set the GPIO as a push/pull output 
  gpio_set_level      (SCL,1);                                                                            // I2C-bus idle mode SDA released (input)
  main_glob_delayI2C  (50);
	//vTaskDelay(1 / portTICK_PERIOD_MS);															                                        // Generate the delay
  if(gpio_get_level(SDA) == HIGH)                                                                         // 
    {                                                                                                     // 
      error = 1;    //  ACK_ERROR;                                                                                  //check ack from i2c slave
      printf("ACK Error\n");                                                                              // Say ACK Error
    }                                                                                                     // 
  gpio_set_level      (SCL,0);                                                                            // I2C-bus idle mode SDA released (input)
  main_glob_delayI2C  (50);
	//vTaskDelay(1 / portTICK_PERIOD_MS);															                                        // Generate the delay
  gpio_set_direction  (SDA, GPIO_MODE_OUTPUT);                                                            // Set the GPIO as a push/pull output 
  return error;                                                                                           // return error code
}                                                                                                         // End of write function
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
unsigned char I2c_ReadByte (etI2cAck ack)                                                                 // Fnction to Start Reading the I2C
{                                                                                                         // Start Reading the I2C
  unsigned char mask,rxByte=0;                                                                            // Get some variables
  gpio_set_level      (SDA,1);                                                                            // I2C-bus idle mode SDA released (input)
  main_glob_delayI2C  (50);
	//vTaskDelay(1 / portTICK_PERIOD_MS);															                                        // Generate the delay

  gpio_set_direction  (SDA, GPIO_MODE_INPUT);                                                             // Set the GPIO as a push/pull output 
  printf("<");
  for (mask=0x80; mask>0; mask>>=1)                                                                       // shift bit for masking (8 times)
  { 
    gpio_set_level      (SCL,1);                                                                          // I2C-bus idle mode SDA released (input)
	  vTaskDelay(20 / portTICK_PERIOD_MS);															                                      // Generate the delay
    if(gpio_get_level(SDA) == 0)                                                                          // if Clear
      {printf("0");}
    else
      {
        rxByte=(rxByte | mask);                                                                             //read bit
        printf("1");
      }
    main_glob_delayI2C  (50);
//    vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
    gpio_set_level      (SCL,0);                                                                          // I2C-bus idle mode SDA released (input)
    main_glob_delayI2C  (50);
//	  vTaskDelay(1 / portTICK_PERIOD_MS);															                                      // Generate the delay
  }
  printf(">");
  gpio_set_direction  (SDA, GPIO_MODE_OUTPUT);                                                            // Set the GPIO as a push/pull output 
  gpio_set_level      (SDA,ack);                                                                          // I2C-bus idle mode SDA released (input)
	vTaskDelay          (4 / portTICK_PERIOD_MS);															                              // Generate the delay
  gpio_set_level      (SCL,1);                                                                            // I2C-bus idle mode SDA released (input)
	vTaskDelay          (4 / portTICK_PERIOD_MS);															                              // Generate the delay
  gpio_set_level      (SCL,0);                                                                            // I2C-bus idle mode SDA released (input)
  gpio_set_level      (SDA,1);                                                                            // I2C-bus idle mode SDA released (input)
  main_glob_delayI2C  (50);
	//vTaskDelay          (1 / portTICK_PERIOD_MS);															                              // Generate the delay
  return rxByte;                                                                                          // return error code
}
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------
/*
void main_glob_delayI2C (unsigned long dly)                                                               // To generate the delay
{                                                                                                         // Start generating the delay
    unsigned char a,b,c;                                                                                  // Variable to get some calculation
    unsigned long Count;                                                                                  // Variable to hold the count
    for(Count = Clear;Count < dly;Count++)                                                                // Wait here and do nothing
      {                                                                                                   // Start getting the delay
          a = Count % 100;                                                                                // 
          b = Count % 200;                                                                                // Do what ever
          c = b - a;                                                                                      // Just do what ever
      }
}                                                                                                         // End of getting the delay
*/
//----------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------