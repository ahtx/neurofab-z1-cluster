/* hw_config.c
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
*/

/*
This file should be tailored to match the hardware design.
Configured for SPI1 on RP2350 with SD card on GPIO40-43
*/

#include "ff.h"  // Include FatFS headers first
#include "hw_config.h"

/* Configuration of hardware SPI object */
static spi_t spi = {
    .hw_inst = spi1,  // SPI1 component for RP2350
    .sck_gpio = 42,   // GPIO42 - CLK
    .mosi_gpio = 43,  // GPIO43 - MOSI  
    .miso_gpio = 40,  // GPIO40 - MISO
    .baud_rate = 125 * 1000 * 1000 / 4  // 31250000 Hz
};

/* SPI Interface */
static sd_spi_if_t spi_if = {
    .spi = &spi,        // Pointer to the SPI driving this card
    .ss_gpio = 41       // GPIO41 - CS (Chip Select)
};

/* Configuration of the SD Card socket object */
static sd_card_t sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if  // Pointer to the SPI interface driving this card
};

/* ********************************************************************** */

size_t sd_get_num() { return 1; }

/**
 * @brief Get a pointer to an SD card object by its number.
 *
 * @param[in] num The number of the SD card to get.
 *
 * @return A pointer to the SD card object, or NULL if the number is invalid.
 */
sd_card_t *sd_get_by_num(size_t num) {
    if (0 == num) {
        // The number 0 is a valid SD card number.
        // Return a pointer to the sd_card object.
        return &sd_card;
    } else {
        // The number is invalid. Return NULL.
        return NULL;
    }
}

/* [] END OF FILE */
