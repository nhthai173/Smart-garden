#pragma once

#ifndef  SCHEDULER_HPP
#define SCHEDULER_HPP

#include <Arduino.h>
#include <vector>
#include <NTPClient.h>

#if defined(ESP8266)
#include <LittleFS.h>
#define SCHEDULE_FS LittleFS
#elif defined(ESP32)
#include <SPIFFS.h>
#define SCHEDULE_FS SPIFFS
#endif

class ScheduleTaskArgsBase;
template <class T = ScheduleTaskArgsBase> class Scheduler;

struct schedule_time_t
{
    uint8_t hour;
    uint8_t minute;
};

struct schedule_repeat_t
{
    bool monday;
    bool tuesday;
    bool wednesday;
    bool thursday;
    bool friday;
    bool saturday;
    bool sunday;
};

template <typename T = ScheduleTaskArgsBase> struct schedule_task_t
{
    uint8_t id;
    schedule_time_t time;
    schedule_repeat_t repeat;
    T* args; // extended from ScheduleTaskArgsBase
    bool enabled;
    bool executed;
};


/**
 * @brief Base class for task arguments. extend this class to add more arguments to the task
 *
 */
class ScheduleTaskArgsBase
{
public:
    ScheduleTaskArgsBase() = default;

    virtual void parse(String& args) { }

    virtual String toString() { return "NULL"; }

};



template <typename T> class Scheduler
{

public:
    String filePath = "/schedules.txt";
    uint8_t MAX_TASKS = 10;
    
    explicit Scheduler(NTPClient *timeClient);
    ~Scheduler();

    /**
     * @brief Add a new task to the schedule
     * 
     * @param task 
     * @return true 
     * @return false 
     */
    bool addTask(schedule_task_t<T> task);
    
    /**
     * @brief Remove a task from the schedule by its id
     * 
     * @param id 
     * @return true 
     * @return false 
     */
    bool removeTask(uint8_t id);
    
    /**
     * @brief Update a task in the schedule by its id
     * 
     * @param id 
     * @param task new task to update
     * @return true 
     * @return false 
     */
    bool updateTask(uint8_t id, schedule_task_t<T> task);
    
    /**
     * @brief Set the Callback object
     * 
     * @param callback 
     */
    void setCallback(std::function<void(schedule_task_t<T>)> callback);
    
    
    /**
     * @brief Get number of tasks in the schedule
     * 
     * @return uint8_t 
     */
    uint8_t getTaskCount();

    /**
     * @brief Load tasks from file
     * 
     */
    bool load();

    /**
     * @brief Overwrite the file with the current tasks
     * 
     * @param overwrite 
     * @return true 
     * @return false 
     */
    bool save();

    /**
     * @brief Run the scheduler
     * 
     */
    void run();

    /**
     * @brief Get the Task By task id
     * 
     * @param id 
     * @return schedule_task_t 
     */
    schedule_task_t<T> getTaskById(uint8_t id);

    /**
     * @brief Print all tasks to serial
     * 
     * @param serial 
     */
    void printToSerial(Stream &stream);


    /**
     * @brief Get the String object
     * 
     * @return String 
     */
    String getString();


    /**
     * @brief Parse a task from string
     * 
     * @param task 
     * @return schedule_task_t 
     */
    static schedule_task_t<T> parseTask(const String& task);


private:
    enum FileMode {
        READ_ONLY = 0x00,
        WRITE_ONLY = 0x01,
        APPEND = 0x02,
        READ_WRITE = 0x03,
    };

    std::vector<schedule_task_t<T>> tasks;
    std::function<void(schedule_task_t<T>)> callback;
    File file;
    NTPClient *timeClient{};

    void openFile(bool overwrite = false, uint8_t mode = READ_WRITE);
    void closeFile();

    /**
     * @brief Append a task to file
     * 
     * @param file file object
     * @param task task object
     * @return true 
     * @return false 
     */
    bool writeTaskToFile(schedule_task_t<T> *task);
    
};

#include "Scheduler.tpp"

#endif //SCHEDULER_HPP