/*!
 * @file
 * @brief Erd list access for various appliances.
 *
 * Ported from PaulGoodJohn's GEA2 Polling Adapter:
 * https://github.com/paulgoodjohn/home-assistant-adapter
 */

#ifndef APPLIANCEERDS_H
#define APPLIANCEERDS_H

#include "tiny_erd.h"

typedef struct {
  const tiny_erd_t *erdList;
  const uint16_t erdCount;
} tiny_erd_list_t;

/*!
 * Get the list of common ERDs (present on all GEA2 appliances).
 */
const tiny_erd_list_t *GetCommonErdList(void);

/*!
 * Get the list of energy reporting ERDs.
 */
const tiny_erd_list_t *GetEnergyErdList(void);

/*!
 * Get the appliance-specific ERD list based on the appliance type byte.
 *
 * @param applianceType The appliance type read from ERD 0x0008.
 */
const tiny_erd_list_t *GetApplianceErdList(uint8_t applianceType);

#endif
