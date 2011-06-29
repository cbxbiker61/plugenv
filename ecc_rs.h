/*
  Interface definition for Reed-Solomon utility functions
*/

#ifndef ECC_RS_H
#define ECC_RS_H

#define ECC_SIZE 10

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * calculate_ecc_rs - Calculate 10 byte Reed-Solomon ECC code for 512 byte block
 * @dat:	raw data
 * @ecc_code:	buffer for ECC
 */
extern int calculate_ecc_rs(const uint8_t *data, uint8_t *ecc_code);

/**
 * correct_data_rs - Detect and correct bit error(s) using 10-byte Reed-Solomon ECC
 * @dat:	raw data read from the chip
 * @store_ecc:	ECC from the chip
 * @calc_ecc:	the ECC calculated from raw data
 */
extern int correct_data_rs(uint8_t *data, uint8_t *store_ecc, uint8_t *calc_ecc);

#ifdef __cplusplus
}
#endif

#endif

