#ifndef N_TIMER_H
#define N_TIMER_H

#include <Arduino.h>
#include <vector>

struct timer_task_t
{
    uint8_t id;
    uint32_t timeout;
    uint32_t last;
    uint8_t repeat;
    std::function<void()> callback;
    bool enabled;
};

struct wait_task_t
{
    bool executed = false;
    uint8_t taskId;
    std::function<bool(void)> condition;
};

class NTimer {

private:
    uint32_t _id_counter = 0;
    std::vector<timer_task_t> _tasks;
    std::vector<wait_task_t> _waitTasks;

public:
    explicit NTimer() = default;
    ~NTimer() = default;

    /**
     * @brief Run the fn after t milliseconds
     * 
     * @param t 
     * @param fn 
     * @return int the id of the timer, -1 if failed
     */
    int setTimeout(uint32_t t, std::function<void()> fn) {
        return setRepeat(t, fn, 1);
    }

    /**
     * @brief Run the fn every t milliseconds
     * 
     * @param t 
     * @param fn 
     * @return int the id of the timer, -1 if failed
     */
    int setInterval(uint32_t t, std::function<void()> fn) {
        return setRepeat(t, fn, 0);
    }

    /**
     * @brief Run the fn every t milliseconds for n times
     * 
     * @param t 
     * @param fn 
     * @param n 
     * @return int the id of the timer, -1 if failed
     */
    int setRepeat(uint32_t t, std::function<void()> fn, uint8_t n) {
        timer_task_t task;
        task.id = _id_counter++;
        task.timeout = t;
        task.last = millis();
        task.repeat = n;
        task.callback = fn;
        task.enabled = true;
        _tasks.push_back(task);
        return task.id;
    }

    void waitFor(std::function<bool(void)> condition, std::function<void()> fn, uint32_t checkInterval = 100) {
        wait_task_t waitTask;
        waitTask.executed = false;
        waitTask.taskId = setInterval(checkInterval, [this, condition, fn, &waitTask]() {
            if (condition()) {
                if (!waitTask.executed) {
                    fn();
                    waitTask.executed = true;
                }
            }
        });
        _waitTasks.push_back(waitTask);
    }

    int restart(uint8_t timerId) {
        for (auto &task : _tasks) {
            if (task.id == timerId) {
                task.last = millis();
                return 0;
            }
        }
        return -1;
    }

    int deleteTimer(uint8_t timerId) {
        for (auto it = _tasks.begin(); it != _tasks.end(); ++it) {
            if (it->id == timerId) {
                _tasks.erase(it);
                return 0;
            }
        }
        return -1;
    }

    timer_task_t *getTimer(uint8_t timerId) {
        for (auto &task : _tasks) {
            if (task.id == timerId) {
                return &task;
            }
        }
        return nullptr;
    }

    int enable(uint8_t timerId) {
        for (auto &task : _tasks) {
            if (task.id == timerId) {
                task.enabled = true;
                return 0;
            }
        }
        return -1;
    }

    int disable(uint8_t timerId) {
        for (auto &task : _tasks) {
            if (task.id == timerId) {
                task.enabled = false;
                return 0;
            }
        }
        return -1;
    }

    void loop() {
        // clean up wait tasks
        if (!_waitTasks.empty()) {
            for (auto it = _waitTasks.begin(); it != _waitTasks.end();) {
                if (it->executed) {
                    deleteTimer(it->taskId);
                    it = _waitTasks.erase(it);
                } else {
                    ++it;
                }
            }
        }

        if (_tasks.empty()) return;
        for (auto &task : _tasks) {
            if (!task.enabled) continue;
            if (millis() - task.last >= task.timeout) {
                task.callback();
                if (task.repeat == 0) {
                    // interval task
                    task.last = millis();
                } else if (task.repeat > 0) {
                    task.repeat--;
                    if (task.repeat == 0) {
                        // remove task
                        deleteTimer(task.id);
                    }
                }
            }
        }
    }
};

#endif // N_TIMER_H
