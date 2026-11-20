idf_component_register(
    SRCS
        "main.c"
        "st7565.c"
         "bms_comp_daq.c"
         "ds18b20.c"
            "SHT2x.c"
        "I2C_HAL.c"
         
    
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_driver_spi
        esp_driver_gpio
           esp_driver_spi
           esp_timer 
           nvs_flash
           esp_driver_uart
              freertos
              esp_driver_i2c
               esp_driver_ledc
               bt
               esp_driver_mcpwm
               driver

)
