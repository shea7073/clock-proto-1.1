#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "clock_manager.h"
#include <time.h>
#include "esp_sntp.h"




#define TAG "Clock-Manager"


// initialize the alarm container
esp_err_t init_alarm_container(alarm_container_t * alarm_container) {
    alarm_container->alarm_list = malloc(INITIAL_MAX_ALARMS * sizeof(alarm_t));
    if (alarm_container->alarm_list == NULL) {
        return ESP_ERR_NO_MEM;
    }
    alarm_container->curr_num_alarms = 0;
    alarm_container->curr_max_alarms = INITIAL_MAX_ALARMS;
    return ESP_OK;
}

esp_err_t add_alarm(alarm_container_t *alarm_container, alarm_t alarm) {
    if (alarm_container->curr_num_alarms == alarm_container->curr_max_alarms) {
        size_t newCapacity = 2 * alarm_container->curr_max_alarms; // Double the capacity
        alarm_t *newCont = realloc(alarm_container->alarm_list, newCapacity * sizeof(alarm_t));
        ESP_LOGI(TAG, "old capacity %d", alarm_container->curr_max_alarms);
        if (newCont == NULL) {
            return ESP_ERR_NO_MEM;
        }
        alarm_container->alarm_list = newCont;
        alarm_container->curr_max_alarms = newCapacity;
        ESP_LOGI(TAG, "new capacity %d", alarm_container->curr_max_alarms);
    }
    alarm_container->alarm_list[alarm_container->curr_num_alarms++] = alarm;
    return ESP_OK;
}

esp_err_t delete_alarm(alarm_container_t *alarm_container, alarm_t alarm) {
    for (int i = 0; i < alarm_container->curr_num_alarms; i ++) {
        if (alarm_container->alarm_list[i].hours == alarm.hours && alarm_container->alarm_list[i].minutes == alarm.minutes) {
            for (int j = i; j < sizeof(alarm_t) * alarm_container->curr_num_alarms - 1; j += sizeof(alarm_t)) {
                alarm_container->alarm_list[i] = alarm_container->alarm_list[i+1];
            }
            alarm_container->curr_num_alarms -= 1;
        } 
    }
    return ESP_OK;
}

void initialize_sntp(void)
{
    ESP_LOGI("SNTP", "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}


void obtain_time(void)
{
    initialize_sntp();

    setenv("TZ", "EST5EDT,M3.2.0/02:00:00,M11.1.0/02:00:00", 1);
    tzset();

    // Wait for time to be set
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI("SNTP", "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void set_time(void) {
    obtain_time();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Log the current time
    ESP_LOGI("SNTP", "Current time: %s", asctime(&timeinfo));

}

int get_minutes(void) {

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "%d", timeinfo.tm_min);
    return  timeinfo.tm_min;
}

int get_hours(void) {

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "%d", timeinfo.tm_hour);
    return  timeinfo.tm_hour;
    
}

esp_err_t alarm_enqueue(alarm_t * alarm, alarm_queue_t * queue) {

    alarm_node_t alarm_node;
    alarm_node.alarm = alarm;
    alarm_node.next = NULL;

    if (queue->size == 0) {
       queue->front = alarm;
       queue->rear = alarm;

    }
    else {
        queue->rear->next = &alarm_node;
        queue->rear = &alarm_node;
    }

     queue->size += 1;

    return ESP_OK;
}

alarm_t * alarm_dequeue(alarm_queue_t * queue) {

    if (queue->size == 0 || queue->front == NULL) {
        return NULL;
    }
    else {
        alarm_t * poppedAlarm = queue->front->alarm;
        queue->front = queue->front->next;
        queue->size -= 1;
        return poppedAlarm;
    }

    
}
