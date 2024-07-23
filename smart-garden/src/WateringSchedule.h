//
// Created by Thái Nguyễn on 24/6/24.
//

#ifndef SMART_GARDEN_WATERINGSCHEDULE_H
#define SMART_GARDEN_WATERINGSCHEDULE_H

#include "Scheduler.h"

class WateringTaskArgs : public ScheduleTaskArgsBase {

public:
    uint8_t valveOpenLevel;     // 1 - 10
    uint8_t waterLiters;        // 1 - 10
    uint8_t duration;           // minutes

    explicit WateringTaskArgs(uint8_t valveLevel = 10, uint8_t liters = 0, uint8_t durations = 1) {
        valveOpenLevel = valveLevel;
        waterLiters = liters;
        duration = durations;
    }

    void parse(String& arg) override {
        int index = arg.indexOf("-");
        valveOpenLevel = arg.substring(0, index).toInt();
        arg = arg.substring(index + 1);
        index = arg.indexOf("-");
        waterLiters = arg.substring(0, index).toInt();
        arg = arg.substring(index + 1);
        duration = arg.toInt();
    }

    String toString() override {
        String str = "";
        str += String(valveOpenLevel) + "-";
        str += String(waterLiters) + "-";
        str += String(duration);
        return str;
    }

};


template class Scheduler<WateringTaskArgs>;

#endif //SMART_GARDEN_WATERINGSCHEDULE_H
