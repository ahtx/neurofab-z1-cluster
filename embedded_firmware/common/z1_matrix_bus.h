/**
 * Z1 Matrix Bus Communication Header
 * 
 * Defines the matrix bus protocol for inter-node spike communication
 */

#ifndef Z1_MATRIX_BUS_H
#define Z1_MATRIX_BUS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize matrix bus
 */
void z1_matrix_bus_init(void);

/**
 * Send spike packet over matrix bus
 * 
 * @param neuron_id Global neuron ID
 * @param value Spike value (0-255)
 * @return true if sent successfully
 */
bool z1_matrix_bus_send_spike(uint16_t neuron_id, uint8_t value);

/**
 * Receive spike packet from matrix bus
 * 
 * @param neuron_id Pointer to store neuron ID
 * @param value Pointer to store spike value
 * @return true if spike received
 */
bool z1_matrix_bus_receive_spike(uint16_t *neuron_id, uint8_t *value);

/**
 * Process matrix bus (poll for incoming spikes)
 */
void z1_matrix_bus_process(void);

/**
 * Get matrix bus status
 * 
 * @return true if initialized and ready
 */
bool z1_matrix_bus_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif // Z1_MATRIX_BUS_H
