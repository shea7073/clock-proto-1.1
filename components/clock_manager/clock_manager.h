#ifndef CLOCK_MANAGER_H
#define CLOCK_MANAGER_H

#define INITIAL_MAX_ALARMS 5

// struct to define an alarm
typedef struct alarm {

    int hours;
    int minutes;
    bool isActive;

} alarm_t;

// struct to hold alarms
typedef struct alarm_container {

    alarm_t * alarm_list;
    size_t curr_num_alarms;
    size_t curr_max_alarms;

} alarm_container_t;

// struct to manage all clock functions
typedef struct clock_manager
{
    alarm_container_t * alarm_container;
    
} clock_manager_t;

esp_err_t init_alarm_container(alarm_container_t * alarm_container);
esp_err_t add_alarm(alarm_container_t *alarm_container, alarm_t alarm);
esp_err_t delete_alarm(alarm_container_t *alarm_container, alarm_t alarm);
void obtain_time(void);
void initialize_sntp(void);
void obtain_time(void);
void set_time(void);
int get_minutes(void);
int get_hours(void);

#endif