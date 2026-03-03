/*!
 * @file
 * @brief ERD list access for various GEA2 appliance types.
 *
 * Matches the ApplianceErds.h interface from paulgoodjohn/home-assistant-adapter
 * so that Gea2MqttBridge.cpp compiles without changes.
 *
 * Implementation (ApplianceErds.cpp) pulls ERD data from src/ErdLists.h which
 * is the comprehensive list already present in this repository.
 */

#ifndef ApplianceErds_h
#define ApplianceErds_h

#include <stdint.h>
#include "tiny_erd.h"

typedef struct
{
  const tiny_erd_t* erdList;
  const uint16_t erdCount;
} tiny_erd_list_t;

/*!
 * Get the list of common ERDs polled for every appliance type.
 */
const tiny_erd_list_t* GetCommonErdList(void);

/*!
 * Get the list of energy-monitoring ERDs.
 */
const tiny_erd_list_t* GetEnergyErdList(void);

/*!
 * Get the appliance-specific ERD list for the given appliance type byte
 * (as returned by ERD 0x0008). Returns the water-heater list for unknown types.
 */
const tiny_erd_list_t* GetApplianceErdList(uint8_t applianceType);

#endif
