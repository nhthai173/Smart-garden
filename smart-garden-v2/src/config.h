//
// Created by Thái Nguyễn on 21/9/25.
//

#ifndef SMART_GARDEN_V2_PIN_CONFIG_H
#define SMART_GARDEN_V2_PIN_CONFIG_H

#define PIN_VALVE_POWER 19          // R1
#define PIN_VALVE_DIRECTION 18      // R2
#define PIN_PUMP_POWER 16           // R3
#define PIN_AC_POWER 4              // R4
#define PIN_WATER_LEAK 34
#define PIN_VOLTAGE 32
#define PIN_FLOW_SENSOR 35

#define OUTPUT_ACTIVE_STATE LOW

#define TIME_AC_AUTO_OFF 180000L        // 3 min
#define TIME_PUMP_POWER_AUTO_OFF 8000L  // 9 sec
#define TIME_PUMP_POWER_ON_DELAY 8000L  // 8 sec
#define TIME_DEFAULT_WATERING 60000L    // 1 min

#define DEVICE_NAME "Watering System"


#endif //SMART_GARDEN_V2_PIN_CONFIG_H
