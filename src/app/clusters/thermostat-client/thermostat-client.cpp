/**
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <app/util/af.h>

#include <app/Command.h>
#include <app/util/af-event.h>
#include <app/util/attribute-storage.h>

#include "gen/attribute-id.h"
#include "gen/attribute-type.h"
#include "gen/cluster-id.h"

using namespace chip;

void emberAfThermostatClusterClientInitCallback(void)
{
    // TODO
}
bool emberAfThermostatClusterCurrentWeeklyScheduleCallback(app::Command * commandObj, uint8_t, uint8_t, uint8_t, uint8_t *)
{
    // TODO
    return false;
}

bool emberAfThermostatClusterRelayStatusLogCallback(app::Command * commandObj, uint16_t, uint16_t, int16_t, uint8_t, int16_t,
                                                    uint16_t)
{
    // TODO
    return false;
}
