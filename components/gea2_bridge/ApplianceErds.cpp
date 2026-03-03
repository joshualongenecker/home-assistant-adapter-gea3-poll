/*!
 * @file
 * @brief ERD list implementation for GEA2 appliance polling.
 *
 * Wraps the ERD data that already lives in src/ErdLists.h.
 * ErdLists.h defines its arrays as 'const' at namespace scope; in C++ those
 * have internal linkage by default, so including from a single translation
 * unit is safe and produces no duplicate-symbol errors.
 *
 * The applianceTypeToErdGroupTranslation table in ErdLists.h uses
 * applianceTypeToErdListAndCount_t which is layout-compatible with
 * tiny_erd_list_t (same fields, same order).  A static_assert below
 * verifies this at compile time.
 */

#include "ApplianceErds.h"
#include <stddef.h>

// Pull in ERD array data from the repository's existing list.
// Path is relative to this source file: components/gea2_bridge/ -> src/
#include "../../src/ErdLists.h"

// Verify that applianceTypeToErdListAndCount_t and tiny_erd_list_t are
// layout-compatible so the reinterpret_cast below is safe.
static_assert(
  sizeof(applianceTypeToErdListAndCount_t) == sizeof(tiny_erd_list_t),
  "applianceTypeToErdListAndCount_t and tiny_erd_list_t must have the same size");
static_assert(
  offsetof(applianceTypeToErdListAndCount_t, erdList) == offsetof(tiny_erd_list_t, erdList),
  "erdList field offset mismatch");
static_assert(
  offsetof(applianceTypeToErdListAndCount_t, erdCount) == offsetof(tiny_erd_list_t, erdCount),
  "erdCount field offset mismatch");

static const tiny_erd_list_t kCommonErdList = { commonErds, commonErdCount };
static const tiny_erd_list_t kEnergyErdList = { energyErds, energyErdCount };

const tiny_erd_list_t* GetCommonErdList(void)
{
  return &kCommonErdList;
}

const tiny_erd_list_t* GetEnergyErdList(void)
{
  return &kEnergyErdList;
}

const tiny_erd_list_t* GetApplianceErdList(uint8_t applianceType)
{
  if(applianceType >= maximumApplianceType) {
    applianceType = 0;
  }
  // applianceTypeToErdGroupTranslation is layout-compatible with tiny_erd_list_t
  // (verified by the static_asserts above).
  return reinterpret_cast<const tiny_erd_list_t*>(
    &applianceTypeToErdGroupTranslation[applianceType]);
}
