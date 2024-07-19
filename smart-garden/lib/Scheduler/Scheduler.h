#pragma once

#include <Arduino.h>
#include <vector>
#include <NTPClient.h>

#define DEBUG_SCHEDULER
#define STORE_SCHEDULES_IN_DATABASE

#if __has_include(<FirebaseClient.h>)

#include <FirebaseClient.h>
#include "FirebaseRTDBIntegrate.h"

#ifndef USE_FIREBASE_RTDB
#define USE_FIREBASE_RTDB
#endif
#endif


#ifdef DEBUG_SCHEDULER
#define DSPrint(...) Serial.print("[Scheduler] "); Serial.printf(__VA_ARGS__)
#else
#define DSPrint(...)
#endif


#ifndef STORE_SCHEDULES_IN_DATABASE

#define STORE_SCHEDULES_IN_FLASH
#if defined(ESP8266)
#include <LittleFS.h>
#define SCHEDULE_FS LittleFS
#elif defined(ESP32)
#include <SPIFFS.h>
#define SCHEDULE_FS SPIFFS
#endif // SCHEDULE_FS

#endif // STORE_SCHEDULES_IN_DATABASE

class ScheduleTaskArgsBase;

template<class T = ScheduleTaskArgsBase>
class Scheduler;

struct schedule_time_t {
    uint8_t hour;
    uint8_t minute;
};

struct schedule_repeat_t {
    bool monday;
    bool tuesday;
    bool wednesday;
    bool thursday;
    bool friday;
    bool saturday;
    bool sunday;
};

template<typename T = ScheduleTaskArgsBase>
struct schedule_task_t {
    uint8_t id;
    schedule_time_t time;
    schedule_repeat_t repeat;
    T *args; // extended from ScheduleTaskArgsBase
    bool enabled;
    bool executed;
};


/**
 * @brief Base class for task arguments. extend this class to add more arguments to the task
 *
 */
class ScheduleTaskArgsBase {
public:
    ScheduleTaskArgsBase() = default;

    virtual void parse(String &args) {}

    virtual String toString() { return "NULL"; }

};


template<typename T>
class Scheduler {

private:

    NTPClient *timeClient = nullptr;
    std::vector<schedule_task_t<T>> tasks;
    std::function<void(schedule_task_t<T>)> _callbackFn = nullptr;

#ifdef STORE_SCHEDULES_IN_FLASH
    File file;
    
    enum FileMode {
        READ_ONLY = 0x00,
        WRITE_ONLY = 0x01,
        APPEND = 0x02,
        READ_WRITE = 0x03,
    };

    void openFile(bool overwrite = false, uint8_t mode = READ_WRITE) {
#if defined(ESP8266)
        if (file && file.isFile()) {
        file.close();
    }
    if (overwrite) {
        this->file = SCHEDULE_FS.open(filePath, "w+");
    } else {
        this->file = SCHEDULE_FS.open(filePath, "a+");
    }
#elif defined(ESP32)
        String modeStr;
        if (mode == WRITE_ONLY) {
            modeStr = FILE_WRITE;
        } else if (mode == APPEND) {
            modeStr = FILE_APPEND;
        } else if (mode == READ_ONLY) {
            modeStr = FILE_READ;
        }
        if (file) {
            file.close();
        }
        if (!SCHEDULE_FS.exists(filePath)) {
            file = SCHEDULE_FS.open(filePath, modeStr.c_str(), true);
            return;
        }
        if (overwrite) {
            file = SCHEDULE_FS.open(filePath, FILE_WRITE, false);
        } else {
            file = SCHEDULE_FS.open(filePath, modeStr.c_str(), false);
        }
#endif
    }


    void closeFile() {
        if (this->file) {
            this->file.close();
        }
    }

    /**
     * @brief Append a task to file
     * 
     * @param file file object
     * @param task task object
     * @return true 
     * @return false 
     */
    bool writeTaskToFile(schedule_task_t<T> *task) {
        openFile(false, APPEND);
        if (!file || file.name() == nullptr) {
            Serial.println("Failed to open file for writing");
            return false;
        }

        file.printf("%d|%d|%d|%d%d%d%d%d%d%d|%s|%d|%d\n",
                    task->id,
                    task->time.hour,
                    task->time.minute,
                    task->repeat.monday,
                    task->repeat.tuesday,
                    task->repeat.wednesday,
                    task->repeat.thursday,
                    task->repeat.friday,
                    task->repeat.saturday,
                    task->repeat.sunday,
                    task->args->toString().c_str(),
                    task->enabled,
                    task->executed);

        closeFile();
        return true;
    }
#elif defined(STORE_SCHEDULES_IN_DATABASE)

    uint8_t MAX_TASKS = 20;
    String db_path;
    fbrtdb_object *dbObj = nullptr;
    AsyncResult _loadResult; // async result for loading tasks

#endif

public:

#ifdef STORE_SCHEDULES_IN_FLASH
    String filePath = "/schedules.txt";
    uint8_t MAX_TASKS = 10;
#endif


#ifdef STORE_SCHEDULES_IN_FLASH

    explicit Scheduler(NTPClient *timeClient, std::function<void(schedule_task_t<T>)> callback = nullptr) {
        this->timeClient = timeClient;
        SCHEDULE_FS.begin();
        // Load tasks from file
        load();
    }

#elif defined(STORE_SCHEDULES_IN_DATABASE)

    Scheduler() = default;

    explicit Scheduler(NTPClient *timeClient, std::function<void(schedule_task_t<T>)> callback = nullptr) {
        this->timeClient = timeClient;
        _callbackFn = callback;
    }

    explicit Scheduler(NTPClient *timeClient, fbrtdb_object *dbObj,
                       std::function<void(schedule_task_t<T>)> callback = nullptr) {
        this->timeClient = timeClient;
        this->dbObj = dbObj;
        _callbackFn = callback;
    }

#endif // contructor


    ~Scheduler() {
        tasks.clear();
        timeClient = nullptr;

#ifdef STORE_SCHEDULES_IN_FLASH
        closeFile();
        SCHEDULE_FS.end();
#elif defined(STORE_SCHEDULES_IN_DATABASE)
        dbObj = nullptr;
#endif

    } // destructor




    /**
     * @brief Load tasks from file/database
     * 
     */
    bool load() {

#ifdef STORE_SCHEDULES_IN_FLASH

        openFile(false, READ_ONLY);
        if (!file || file.name() == nullptr) {
            Serial.println("Failed to open file for reading");
            return false;
        }

        // Clear tasks
        tasks.clear();

        // Read file
        if (file.size() == 0) {
            closeFile();
            return true;
        }

        String line;
        while (file.available()) {
            line = file.readStringUntil('\n');
            if (line.length() > 0) {
                auto task = parseTask(line);
                if (task.id)
                    tasks.push_back(task);
            }
        }

        closeFile();
        return true;

#elif defined(STORE_SCHEDULES_IN_DATABASE)

        if (dbObj == nullptr) {
            DSPrint("Database object is not attached\n");
            return false;
        }
        if (getPath() == "") {
            DSPrint("Database path is not set\n");
            return false;
        }

        DSPrint("Start loading tasks from database\n");
        DSPrint("> Path: %s\n", getPath().c_str());
        _loadResult.clear();
        dbObj->rtdb->get(*dbObj->client, getPath(), _loadResult);
        return true; // always true because it is async

#endif

    } // load




    /**
 * @brief Overwrite the file with the current tasks
 *
 * @param overwrite
 * @return true
 * @return false
 */
    bool save() {

#ifdef STORE_SCHEDULES_IN_FLASH

        DSPrint("Saving tasks to file\n");
        openFile(true, WRITE_ONLY);
        if (!file || file.name() == nullptr) {
            DSPrint("Failed to open file for writing\n");
            return false;
        }
        closeFile();

        for (auto &task: tasks) {
            writeTaskToFile(&task);
        }
        DSPrint("Tasks saved to file\n");

#elif defined(STORE_SCHEDULES_IN_DATABASE)

        String tasksArrayStr = toArray();
        if (tasksArrayStr == "[]") {
            // Schedule is empty, remove all tasks from database
            DSPrint("Removing all tasks from database\n");
            dbObj->rtdb->remove(
                    *dbObj->client,
                    getPath(),
                    [](AsyncResult &res) {
                        if (res.isError()) {
                            DSPrint("> Failed to remove all tasks from database\n");
                            DSPrint(">> msg: %s, code: %d\n", res.error().message().c_str(), res.error().code());
                            return;
                        }
                        DSPrint("> All tasks removed from database\n");
                    });
        } else {
            DSPrint("Saving tasks to database\n");
            DSPrint(">>>>>>>>> \n%s\n", tasksArrayStr.c_str());
            dbObj->rtdb->set<object_t>(
                    *dbObj->client,
                    getPath(),
                    (object_t) tasksArrayStr,
                    [](AsyncResult &res) {
                        if (res.isError()) {
                            DSPrint("> Failed to save tasks to database\n");
                            DSPrint(">> msg: %s, code: %d\n", res.error().message().c_str(), res.error().code());
                            return;
                        }
                        DSPrint("> Tasks saved to database\n");
                    });

        }

        return true;
#endif

    } // save





    /**
     * @brief Add a new task to the schedule
     * 
     * @param task 
     * @return true 
     * @return false 
     */
    bool addTask(schedule_task_t<T> task) {
        DSPrint("Adding task to schedule\n");
        if (tasks.size() >= MAX_TASKS) {
            DSPrint("Max tasks reached\n");
            return false;
        }
        tasks.push_back(task);
#ifdef STORE_SCHEDULES_IN_FLASH
        return writeTaskToFile(&task);
#elif defined(STORE_SCHEDULES_IN_DATABASE)
        DSPrint("Syncing task to database\n");
        return save();
#endif

    }

    /**
     * @brief Remove a task from the schedule by its id
     * 
     * @param id 
     * @return true 
     * @return false 
     */
    bool removeTask(uint8_t id) {
        DSPrint("Removing task [%d] from schedule\n", id);
        bool found = false;
        for (auto it = tasks.begin(); it != tasks.end(); ++it) {
            if (it->id == id) {
                delete it->args;
                tasks.erase(it);
                found = true;
                break;
            }
        }
        if (!found) {
            DSPrint("> Task not found\n");
            return false;
        }
        DSPrint("> Task removed, syncing to database\n");
        return save();
    }

    /**
     * @brief Update a task in the schedule by its id
     * 
     * @param id 
     * @param task new task to update
     * @return true 
     * @return false 
     */
    bool updateTask(uint8_t id, schedule_task_t<T> task) {
        DSPrint("Updating task [%d] in schedule\n", id);
        for (auto t: tasks) {
            if (t.id == id) {
                t = task;
                DSPrint("> Task updated, syncing to database\n");
                return save();
            }
        }
        DSPrint("> Task not found\n");
        return false;
    }


    /**
     * @brief Get the Task By task id
     *
     * @param id
     * @return schedule_task_t
     */
    schedule_task_t<T> getTaskById(uint8_t id) {
        for (auto task: tasks) {
            if (task.id == id) {
                return task;
            }
        }
        return {};
    }


    /**
     * @brief Get number of tasks in the schedule
     * 
     * @return uint8_t 
     */
    uint8_t getTaskCount() {
        return tasks.size();
    }


#ifdef STORE_SCHEDULES_IN_DATABASE

    /**
     * @brief Get the path to store schedules in database
     * @return
     */
    String getPath() const {
        if (db_path.length())
            return db_path;
        if (dbObj)
            return dbObj->prefixPath;
        return "";
    }

    /**
     * @brief Set the path to store schedules in database (not use prefix path)
     * @param path
     */
    void setPath(const String &path) {
        db_path = path;
    }

    /**
     * @brief Attach a database to store schedules
     * @param database
     * @param path path to store schedules in database (not use prefix path)
     */
    void attachDatabase(fbrtdb_object *database, const String &path = "") {
        dbObj = database;
        if (path.length() > 0) {
            db_path = path;
        }
    }

#endif


    /**
     * @brief Set the NTPClient object
     * @param client
     */
    void setNTPClient(NTPClient *client) {
        this->timeClient = client;
    }


    /**
     * @brief Set the Callback object
     *
     * @param callback
     */
    void setCallback(std::function<void(schedule_task_t<T>)> callback) {
        _callbackFn = callback;
    }


    /**
     * @brief Run the scheduler
     * 
     */
    void run() {

#ifdef STORE_SCHEDULES_IN_DATABASE
        /* ========= Parse Task ========= */

        if (_loadResult.isError()) {
            DSPrint("Failed to load tasks from database\n");
            DSPrint("> msg: %s, code: %d\n", _loadResult.error().message().c_str(), _loadResult.error().code());
            _loadResult.clear();
            return;
        } else if (_loadResult.available()) {
            DSPrint("Got response from database\n");
            DSPrint("Payload: %s\n", _loadResult.payload().c_str());

            // Remove [ and ]
            String tasksString = _loadResult.payload().substring(1, _loadResult.payload().length() - 1);
            tasksString.replace("\"", "");

            tasks.clear();

            const char *start = tasksString.c_str();
            const char *ptr = tasksString.c_str();
            uint32_t index = 0;
            while (true) {
                uint32_t cur_index = ptr - start;
                if (*ptr == ',' || cur_index == tasksString.length()) {
                    String taskStr = tasksString.substring(index, cur_index);
                    index = ptr - start + 1;

                    DSPrint("> Parsing task: %s\n", taskStr.c_str());

                    auto task = parseTask(taskStr);
                    if (task.id) {
                        tasks.push_back(task);
                        DSPrint(">> Task parsed successfully\n");
                        if (tasks.size() >= MAX_TASKS) {
                            DSPrint(">> Max tasks reached. Stop parsing\n");
                            break;
                        }
                    } else {
                        DSPrint(">> Task parsing failed\n");
                    }
                }
                if (cur_index == tasksString.length()) {
                    break;
                }
                ptr++;
            }

            DSPrint("Tasks loaded from database (%d tasks)\n", tasks.size());
            _loadResult.clear();

#ifdef DEBUG_SCHEDULER
            printToSerial(Serial);
#endif

        }

#endif // STORE_SCHEDULES_IN_DATABASE



        /* ========= Run ========= */

        if (tasks.empty() || timeClient == nullptr) {
            return;
        }

        if (!timeClient->isTimeSet()) {
            timeClient->begin();
            timeClient->update();
            return;
        }

        uint8_t h = timeClient->getHours();
        uint8_t m = timeClient->getMinutes();
        uint8_t dow = timeClient->getDay(); // 0 is sunday
        bool anychange = false;

        for (auto &task: tasks) {
            if (task.enabled) {
                if ((task.repeat.monday && dow == 1) ||
                    (task.repeat.tuesday && dow == 2) ||
                    (task.repeat.wednesday && dow == 3) ||
                    (task.repeat.thursday && dow == 4) ||
                    (task.repeat.friday && dow == 5) ||
                    (task.repeat.saturday && dow == 6) ||
                    (task.repeat.sunday && dow == 0)) {
                    if (task.time.hour == h && task.time.minute == m) {
                        if (!task.executed) {
                            task.executed = true;
                            anychange = true;
                            if (_callbackFn)
                                _callbackFn(task);
                        }
                    } else if (task.executed) {
                        task.executed = false;
                        anychange = true;
                    }
                }
            }
        }

        if (anychange) {
            save();
        }
    }


    /**
     * @brief Print all tasks to serial
     * 
     * @param serial 
     */
    void printToSerial(Stream &stream) {
        stream.printf("Task count: %d\n\n", tasks.size());
        for (auto &task: tasks) {
            stream.printf("Task %d: %02d:%02d\nrepeat: %d|%d|%d|%d|%d|%d|%d\nargs: %s\nenabled: %d\nexecuted: %d\n\n",
                          task.id,
                          task.time.hour,
                          task.time.minute,
                          task.repeat.monday,
                          task.repeat.tuesday,
                          task.repeat.wednesday,
                          task.repeat.thursday,
                          task.repeat.friday,
                          task.repeat.saturday,
                          task.repeat.sunday,
                          task.args->toString().c_str(),
                          task.enabled,
                          task.executed);
        }

#ifdef STORE_SCHEDULES_IN_FLASH
        Serial.println("======== Read from file ========");
        openFile(false, READ_ONLY);

        Serial.printf("File size: %d\n", file.size());
        while (file.available()) {
            Serial.print("[Task] ");
            Serial.println(file.readStringUntil('\n'));
        }

        Serial.println("=========== End file ===========");
        closeFile();
#endif

    } // printToSerial



#ifdef STORE_SCHEDULES_IN_FLASH

    /**
         * @brief Get the String object
         *
         * @return String
         */
        String getString() {
            openFile(false, READ_ONLY);
            String result = file.readString();
            closeFile();
            if (result.length() == 0) {
                result = "EMPTY";
            }
            return result;
        }

#elif defined(STORE_SCHEDULES_IN_DATABASE)

    /**
     * @brief Get the schedules as array string
     * @return
     */
    String toArray() {
        String tasksArrayStr = "";
        for (auto task: tasks) {
            tasksArrayStr += "\"";
            tasksArrayStr += String(task.id) + "|";
            tasksArrayStr += String(task.time.hour) + "|";
            tasksArrayStr += String(task.time.minute) + "|";
            tasksArrayStr += task.repeat.monday ? "1" : "0";
            tasksArrayStr += task.repeat.tuesday ? "1" : "0";
            tasksArrayStr += task.repeat.wednesday ? "1" : "0";
            tasksArrayStr += task.repeat.thursday ? "1" : "0";
            tasksArrayStr += task.repeat.friday ? "1" : "0";
            tasksArrayStr += task.repeat.saturday ? "1" : "0";
            tasksArrayStr += String(task.repeat.sunday ? "1" : "0") + "|";
            tasksArrayStr += task.args->toString() + "|";
            tasksArrayStr += String(task.enabled ? "1" : "0") + "|";
            tasksArrayStr += String(task.executed ? "1" : "0") + "\",";
        }
        if (tasksArrayStr.length() > 0) {
            // Remove last comma
            tasksArrayStr = tasksArrayStr.substring(0, tasksArrayStr.length() - 1);
            tasksArrayStr = "[" + tasksArrayStr + "]";
            return tasksArrayStr;
        }
        return "[]";
    }

#endif


    /**
     * @brief Parse a task from string
     * 
     * @param task 
     * @return schedule_task_t 
     */
    static schedule_task_t<T>
    parseTask(const String &task) {
        schedule_task_t<T> result{};
        result.id = 0;
        result.args = new T();
        uint8_t count = 0;
        int last_pos = 0;
        std::vector<uint8_t> values;
        String repeat = "";
        String args = "";

        // Task format: id|hour|minute|repeat|args|enabled|executed

        while ((unsigned int) last_pos < task.length()) {
            int pos = task.indexOf("|", last_pos);
            if (pos < 0) pos = task.length();

            // repeat part
            if (count == 3) {
                repeat = task.substring(last_pos, pos);
            }
                // args part
            else if (count == 4) {
                args = task.substring(last_pos, pos);
            }
                // other parts
            else {
                uint8_t num = (uint8_t) task.substring(last_pos, pos).toInt();
                values.push_back(num);
            }
            last_pos = pos + 1;
            ++count;
        }

        if (values.size() != 5 || repeat == "" || args == "") {
            Serial.println("Invalid task format");
            return result;
        }

        // Parse repeat days
        result.repeat.monday = repeat.charAt(0) == '1';
        result.repeat.tuesday = repeat.charAt(1) == '1';
        result.repeat.wednesday = repeat.charAt(2) == '1';
        result.repeat.thursday = repeat.charAt(3) == '1';
        result.repeat.friday = repeat.charAt(4) == '1';
        result.repeat.saturday = repeat.charAt(5) == '1';
        result.repeat.sunday = repeat.charAt(6) == '1';

        // Parse args
        result.args->parse(args);

        // Assign values
        result.id = values[0];
        result.time.hour = values[1];
        result.time.minute = values[2];
        result.enabled = values[3];
        result.executed = values[4];

        return result;
    }

    /**
     * @brief Parse time from string format "HH:MM"
     * @param time
     * @return schedule_time_t
     */
    static schedule_time_t parseTime(const String &time) {
        schedule_time_t result{};
        int pos = time.indexOf(":");
        if (pos < 0) {
            return result;
        }
        result.hour = (uint8_t) time.substring(0, pos).toInt();
        result.minute = (uint8_t) time.substring(pos + 1).toInt();
        return result;
    }

    /**
     * @brief generate a unique id for a task
     * @return
     */
    uint8_t generateUid() {
        uint8_t id = 0;
        do {
            id = static_cast<uint8_t>(rand());
        } while (getTaskById(id).id != 0);
        return id;
    }
};