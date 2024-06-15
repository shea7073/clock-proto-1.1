#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs_flash.h"
#include "wifi_connect.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_http_server.h"
#include "sdmmc_cmd.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "errno.h"
#include "clock_manager.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/i2s_std.h"

#include <math.h>
#include "main.h"


#define TAG "Clock-Main"

#define PIN_CS GPIO_NUM_4
#define PIN_MOSI GPIO_NUM_5
#define PIN_CLK GPIO_NUM_18
#define PIN_MISO GPIO_NUM_19

// 18 and 19 need to be reassigned (update - seems to have worked with new vals)
// #define LATCH GPIO_NUM_18
// #define CLOCK GPIO_NUM_19                                                                                                                                                 

#define LATCH GPIO_NUM_22
#define CLOCK GPIO_NUM_23
#define DATA  GPIO_NUM_21

#define SWITCH GPIO_NUM_13

#define I2S_BCLK GPIO_NUM_26
#define I2S_WS GPIO_NUM_25
#define I2S_DOUT GPIO_NUM_27

#define I2S_NUM           (0)
#define I2S_SAMPLE_RATE   (44100)
#define I2S_SAMPLE_BITS   (16)
#define I2S_CHANNEL_NUM   (2)

// these are set backwards so they start at dot pixel and move from G-A
int digits_rev[10][8] =  {
    {0, 0, 1, 1, 1, 1, 1, 1}, // 0
    {0, 0, 0, 0, 0, 1, 1, 0}, // 1
    {0, 1, 0, 1, 1, 0, 1, 1}, // 2
    {0, 1, 0, 0, 1, 1, 1, 1}, // 3
    {0, 1, 1, 0, 0, 1, 1, 0}, // 4
    {0, 1, 1, 0, 1, 1, 0, 1}, // 5
    {0, 1, 1, 1, 1, 1, 0, 1}, // 6
    {0, 0, 0, 0, 0, 1, 1, 1}, // 7
    {0, 1, 1, 1, 1, 1, 1, 1}, // 8
    {0, 1, 1, 0, 1, 1, 1, 1}  // 9
};

alarm_container_t alarm_container;
clock_manager_t clock_manager;

SemaphoreHandle_t queue_mutex;
SemaphoreHandle_t alarm_mutex;

alarm_queue_t alarm_queue;

QueueHandle_t interruptQueue;

TaskHandle_t alarm_handle;

static i2s_chan_handle_t tx_chan;        // I2S tx channel handler

// for i2s data 
int16_t * buf;
// audio file
FILE* f;


void init_display_gpio(void) {

    gpio_set_direction(LATCH, GPIO_MODE_OUTPUT);
    gpio_set_level(LATCH, 0);
    vTaskDelay(1 / portTICK_PERIOD_MS);

    gpio_set_direction(CLOCK, GPIO_MODE_OUTPUT);
    gpio_set_level(CLOCK, 0);
    vTaskDelay(1 / portTICK_PERIOD_MS);

    gpio_set_direction(DATA, GPIO_MODE_OUTPUT);
    gpio_set_level(DATA, 0);
    vTaskDelay(1 / portTICK_PERIOD_MS);

}

void start_mdns() {
    mdns_init();
    mdns_hostname_set("seans-alarm");
    mdns_instance_name_set("Alarm-Proto-1.0");
}

// URL handlers

static esp_err_t on_default_url(httpd_req_t *req) {
    ESP_LOGI(TAG, "URI: %s", req->uri);

    char path[600];
    sprintf(path, "/sdcard%s", req->uri);

    // set mime type by matching the extension
    char *ext = strrchr(req->uri, '.');
   if (ext != NULL){
        ESP_LOGI(TAG, "%s", ext);
        if (strcmp(ext, ".css") == 0) {
        httpd_resp_set_type(req, "text/css");
        }
        if (strcmp(ext, ".js") == 0) {
            httpd_resp_set_type(req, "text/javascript");
        }
        if (strcmp(ext, ".png") == 0) {
            httpd_resp_set_type(req, "image/png");
        }
        if (strcmp(ext, ".jpg") == 0) {
            httpd_resp_set_type(req, "image/jpg");
        }
        if (strcmp(ext, ".svg") == 0) {
            httpd_resp_set_type(req, "image/svg+xml");
        }
    }
    // open requested file
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        file = fopen("/sdcard/index.html", "r");
        if (file == NULL) {
            httpd_resp_send_404(req);
        }
    }
    char buffer[1024];

    int bytes_read = 0;

     while ((bytes_read = fread(buffer, sizeof(char), sizeof(buffer), file)) > 0) {
        httpd_resp_send_chunk(req, buffer, bytes_read);
     }

    fclose(file);

    httpd_resp_send_chunk(req, NULL, 0);
    //httpd_resp_sendstr(req, "hello_world");
    return ESP_OK;
}
    



static esp_err_t on_set_alarm(httpd_req_t *req) {

    alarm_t alarm;   
    
    char data[200];

    int ret, remaining = req->content_len;

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, data, MIN(remaining, sizeof(data)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }

        /* Process the JSON data */
        cJSON *root = cJSON_Parse(data);
        if (root == NULL) {
            ESP_LOGE(TAG, "JSON parse error");
            return ESP_FAIL;
        }

        cJSON *hours = cJSON_GetObjectItem(root, "hours");
        if (hours) {
            ESP_LOGI(TAG, "Hours: %d", hours->valueint);
        }

        cJSON *minutes = cJSON_GetObjectItem(root, "minutes");
        if (minutes) {
            ESP_LOGI(TAG, "minutes: %d", minutes->valueint);
        }

        if (minutes && hours) {
            int n = clock_manager.alarm_container->curr_num_alarms;
            for (int i = 0; i < n; i++) {
                ESP_LOGI(TAG,"loop is on %d", i);
                if (minutes->valueint == clock_manager.alarm_container->alarm_list[i].minutes 
                && hours->valueint == clock_manager.alarm_container->alarm_list[i].hours) {
                    //const char* resp = "Duplicate Alarm Cannot Be Set!";
                    //httpd_resp_send(req, resp, strlen(resp));
                    
                    httpd_resp_send_404(req);
                    return ESP_ERR_INVALID_ARG;
                }
            }
            alarm.minutes = minutes->valueint;
            alarm.hours = hours->valueint;
            alarm.isActive = true;
            // need to check existing alarms first
            add_alarm(clock_manager.alarm_container, alarm);

        // TESTING ---------

            ESP_LOGI(TAG, "num alarms: %d", clock_manager.alarm_container->curr_num_alarms);
        // END TESTING --------

        }

        cJSON_Delete(root);
        remaining -= ret;
    }

    // Send response
    const char* resp = "Alarm Set!";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t on_get_alarms(httpd_req_t *req) {

    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateArray();

    int num_alarms = clock_manager.alarm_container->curr_num_alarms;

    for (int i = 0; i < num_alarms; i += 1){

        cJSON *alarm = cJSON_CreateObject();

        cJSON_AddNumberToObject(alarm, "hours", clock_manager.alarm_container->alarm_list[i].hours);
        cJSON_AddNumberToObject(alarm, "minutes", clock_manager.alarm_container->alarm_list[i].minutes);
        cJSON_AddNumberToObject(alarm, "isActive", clock_manager.alarm_container->alarm_list[i].isActive);   
        cJSON_AddItemToArray(root, alarm);
    }

    const char *response = cJSON_Print(root);
    
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;

};

static esp_err_t on_delete_alarm(httpd_req_t *req) {

    alarm_t alarm;
    
    char data[200];

    int ret, remaining = req->content_len;

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, data, MIN(remaining, sizeof(data)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }

        /* Process the JSON data */
        cJSON *root = cJSON_Parse(data);
        if (root == NULL) {
            ESP_LOGE(TAG, "JSON parse error");
            return ESP_FAIL;
        }

        cJSON *hours = cJSON_GetObjectItem(root, "hours");
        if (hours) {
            ESP_LOGI(TAG, "Hours: %d", hours->valueint);
        }

        cJSON *minutes = cJSON_GetObjectItem(root, "minutes");
        if (minutes) {
            ESP_LOGI(TAG, "minutes: %d", minutes->valueint);
        }

        if (minutes && hours) {
            int n = clock_manager.alarm_container->curr_num_alarms;
            for (int i = 0; i < n; i++) {
                ESP_LOGI(TAG,"loop is on %d", i);
                if (minutes->valueint == clock_manager.alarm_container->alarm_list[i].minutes 
                && hours->valueint == clock_manager.alarm_container->alarm_list[i].hours) {
                    alarm.minutes = minutes->valueint;
                    alarm.hours = hours->valueint;
                    alarm.isActive = true;
                    delete_alarm(clock_manager.alarm_container, alarm);
                    const char* resp = "Deleted Alarm!";
                    httpd_resp_send(req, resp, strlen(resp));
                    cJSON_Delete(root);
                    return ESP_OK;
            }
            }}

        // TESTING ---------

            ESP_LOGI(TAG, "num alarms: %d", clock_manager.alarm_container->curr_num_alarms);
        // END TESTING --------

        cJSON_Delete(root);
        remaining -= ret;
    
}
    return ESP_ERR_INVALID_ARG;
};

static esp_err_t on_toggle_alarm(httpd_req_t *req) {
    
    char data[200];

    int ret, remaining = req->content_len;

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, data, MIN(remaining, sizeof(data)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }

        /* Process the JSON data */
        cJSON *root = cJSON_Parse(data);
        if (root == NULL) {
            ESP_LOGE(TAG, "JSON parse error");
            return ESP_FAIL;
        }

        cJSON *hours = cJSON_GetObjectItem(root, "hours");
        if (hours) {
            ESP_LOGI(TAG, "Hours: %d", hours->valueint);
        }

        cJSON *minutes = cJSON_GetObjectItem(root, "minutes");
        if (minutes) {
            ESP_LOGI(TAG, "minutes: %d", minutes->valueint);
        }


        cJSON *isActive = cJSON_GetObjectItem(root, "isActive");
        if (isActive != NULL) {
            ESP_LOGI(TAG, "isActive: %d", isActive->valueint);
        }


        if (minutes && hours) {
            if ((minutes->valueint != get_minutes()) || (hours->valueint != get_hours())){
            
                int n = clock_manager.alarm_container->curr_num_alarms;
                for (int i = 0; i < n; i++) {
                    if (minutes->valueint == clock_manager.alarm_container->alarm_list[i].minutes 
                    && hours->valueint == clock_manager.alarm_container->alarm_list[i].hours) {
                        clock_manager.alarm_container->alarm_list[i].isActive = (bool)isActive->valueint;
                        const char* resp = "Toggled Alarm!";
                        httpd_resp_send(req, resp, strlen(resp));
                        cJSON_Delete(root);
                        return ESP_OK;
            }}
        
            }
            }
       
            
        cJSON_Delete(root);
        remaining -= ret;
    
}
    httpd_resp_send_404(req);
    return ESP_ERR_INVALID_ARG;
}




static void init_server() {

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t toggle_alarm_url = {
        .uri = "/toggleAlarm",
        .method = HTTP_POST,
        .handler = on_toggle_alarm
    };

    httpd_register_uri_handler(server, &toggle_alarm_url);



    httpd_uri_t set_alarm_url = {
        .uri = "/setAlarm",
        .method = HTTP_POST,
        .handler = on_set_alarm
    };

    httpd_register_uri_handler(server, &set_alarm_url);

    httpd_uri_t get_alarms_url = {
        .uri = "/getAlarms",
        .method = HTTP_GET,
        .handler = on_get_alarms
    };

    httpd_register_uri_handler(server, &get_alarms_url);

     httpd_uri_t delete_alarm_url = {
        .uri = "/deleteAlarm",
        .method = HTTP_POST,
        .handler = on_delete_alarm
    };

    httpd_register_uri_handler(server, &delete_alarm_url);

   
    httpd_uri_t default_url = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = on_default_url
    };

    httpd_register_uri_handler(server, &default_url);


}

void mount_sdcard(void) {
     // mount config for sd card reader
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // config the spi bus
    spi_bus_config_t spi_bus_config = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_CLK,
        .quadhd_io_num = -1,
        .quadwp_io_num = -1
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &spi_bus_config, SDSPI_DEFAULT_DMA));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_CS;
    slot_config.host_id = host.slot;

    sdmmc_card_t *card;
    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card));

    sdmmc_card_print_info(stdout, card);

    // unmount sd card
    //esp_vfs_fat_sdmmc_unmount(BASE_PATH, &card);
    // free up the spi bus
    //spi_bus_free(host.slot);
}


void displayTime(void * params) {

     while (true) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        int hours = timeinfo.tm_hour;
        int minutes = timeinfo.tm_min;
        int seconds = timeinfo.tm_sec;

        int curr_digits[4];
        if (hours < 10) {
            curr_digits[0] = 0;
            curr_digits[1] = hours;
        }

        else {
           curr_digits[0] = (int)floor(hours / 10);
            curr_digits[1] = hours % 10;
        }

        if (minutes < 10) {
            curr_digits[2] = 0;
            curr_digits[3] = minutes;
        }

        else {
            curr_digits[2] = (int)floor(minutes / 10);
            curr_digits[3] = minutes % 10;
        }

         for (int i = 3; i >= 0; i--) {
            for (int j = 0; j < 8; j++) {
                gpio_set_level(LATCH, 0);
                vTaskDelay(10 / portTICK_PERIOD_MS);
                ESP_ERROR_CHECK(gpio_set_level(DATA, digits_rev[curr_digits[i]][j]));
                vTaskDelay(10 / portTICK_PERIOD_MS);
                gpio_set_level(CLOCK, 1);
                vTaskDelay(10 / portTICK_PERIOD_MS);
                gpio_set_level(CLOCK, 0);
                vTaskDelay(10 / portTICK_PERIOD_MS);
                // ESP_LOGI(TAG, "%d", digits_rev[i][j]);

            }
            
            
        }
        gpio_set_level(LATCH, 1);
        vTaskDelay(10 / portTICK_PERIOD_MS);

        // Log the current time
        ESP_LOGI("Sending Time", "Hours: %d, Minutes: %d, Seconds: %d", hours, minutes, seconds);
        //vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}


void alarm_triggered_task(void * params) {
    
    buf = allocate_i2s_buffer();
    f = open_wav("/sdcard/Mac_Miller-Once-A-Day.wav");

    play_wav(buf, f);

    // Trigger song or alarm noise
    while(true){
        ESP_LOGI("ALARM-TRIGGERED", "Wake up!");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        
    
}

void alarmMonitor(void * params) {

    while (true) {
        ESP_LOGI("Monitor", "Queue size: %d", alarm_queue.size);
        int n = clock_manager.alarm_container->curr_num_alarms;
        for (int i = 0; i < n; i++) {
            if (clock_manager.alarm_container->alarm_list[i].hours == get_hours() &&
            clock_manager.alarm_container->alarm_list[i].minutes == get_minutes() &&
            clock_manager.alarm_container->alarm_list[i].isActive) {
                if(xSemaphoreTake(queue_mutex, portMAX_DELAY)) {
                    alarm_enqueue(&clock_manager.alarm_container->alarm_list[i], &alarm_queue);
                    clock_manager.alarm_container->alarm_list[i].isActive = false;
                    xSemaphoreGive(queue_mutex);
                }
                else {
                    ESP_LOGI("Alarm Monitor", "Could not access alarm queue");
                }
                
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}

void alarmSpawner(void * params) {

    while (true) {
        if (alarm_queue.size > 0 && alarm_handle == NULL) {
            //if (alarm_queue.size > 0) {
            if (xSemaphoreTake(queue_mutex, portMAX_DELAY)) {
                    alarm_t * alarm = alarm_dequeue(&alarm_queue);
                    xSemaphoreGive(queue_mutex);
                    xTaskCreatePinnedToCore(&alarm_triggered_task, "trigger-alarm", 4048, NULL, 1, &alarm_handle, 1);
                }
                else {
                    ESP_LOGI("Alarm Spawner", "Could not access alarm queue");
                }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}

static void IRAM_ATTR gpio_isr_handler(void *args) {
    char mesg[] = "buttonPushedTask";
    xQueueSendFromISR(interruptQueue, &mesg, NULL);
}

void buttonPushedTask(void *params) {

    char mesg[17];

    while (true)
    {
        if (xQueueReceive(interruptQueue, &mesg, portMAX_DELAY) == pdTRUE)
        {
            // disable the interrupt
            gpio_isr_handler_remove(SWITCH);

            // wait some time while we check for the button to be released
            do
            {
                vTaskDelay(20 / portTICK_PERIOD_MS);
            } while (gpio_get_level(SWITCH) == 1);

             if (alarm_handle != NULL) {
                ESP_LOGI("ISR", " : %s", mesg);
                i2s_channel_disable(tx_chan);
                free(buf);
                fclose(f);
                vTaskDelete(alarm_handle);
                alarm_handle = NULL;
                
                }

            // re-enable the interrupt
            gpio_isr_handler_add(SWITCH, gpio_isr_handler, (void*)NULL);
        }
    }
}


void alarm_isr_setup() {
    // set gpio for button input
    gpio_set_direction(SWITCH, GPIO_MODE_INPUT);
    gpio_pulldown_en(SWITCH);
    gpio_pullup_dis(SWITCH);
    gpio_set_intr_type(SWITCH, GPIO_INTR_POSEDGE);
    
    interruptQueue = xQueueCreate(10, sizeof(int));
    xTaskCreate(&buttonPushedTask, "buttonPushedTask", 4048, NULL, 1, NULL);
    ESP_LOGI("ISR", "Got here");

    gpio_install_isr_service(0);
    gpio_isr_handler_add(SWITCH, gpio_isr_handler, (void*)NULL);

}

void init_i2s() {
    
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // some codecs may require mclk signal, this example doesn't need it
            .bclk = I2S_BCLK,
            .ws   = I2S_WS,
            .dout = I2S_DOUT,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
}

void play_wav(int16_t * buf, FILE* f) {
    
    fseek(f, 44, SEEK_SET);
    
    size_t bytes_read = 0;
    size_t bytes_written = 0;

    bytes_read = fread(buf, sizeof(int16_t), 2048, f);

    i2s_channel_enable(tx_chan);

  while (bytes_read > 0)
  {
    // write the buffer to the i2s
    i2s_channel_write(tx_chan, buf, bytes_read * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    bytes_read = fread(buf, sizeof(int16_t), 2048, f);
    ESP_LOGV(TAG, "Bytes read: %d", bytes_read);
  }

    i2s_channel_disable(tx_chan);
    free(buf);
    fclose(f);
}

int16_t * allocate_i2s_buffer() {
    // create a writer buffer
    int16_t *buf = calloc(2048, sizeof(int16_t));
    return buf;
}

FILE* open_wav(char * path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGI(TAG, "Failed to open file for reading\n");
        return NULL;
    }
    else {
        return f;
    }
}


void app_main(void)
{
    // mount the sdcard
    mount_sdcard();
   
    // initialize flash
    ESP_ERROR_CHECK(nvs_flash_init());

    //initialize wifi
    wifi_connect_init();

    ESP_ERROR_CHECK(wifi_connect_sta("143", "Sweetpea", 100000));

    // start mdns for local DNS name 
    start_mdns();

    // start http server
    init_server();

    // set current time
    set_time();

    queue_mutex = xSemaphoreCreateMutex();
    alarm_mutex = xSemaphoreCreateMutex();
    
    // initialize the alarm container
    init_alarm_container(&alarm_container);
    
    // add alarm container to the clock mif(minutes != get_minutes() && hours != get_hours()) {ager struct
    clock_manager.alarm_container = &alarm_container;

    // initialize gpios for 7 segment display control
    init_display_gpio();


    // task for sending time to displays
    xTaskCreatePinnedToCore(&displayTime, "display-time", 4048, NULL, 1, NULL, 1);

     // set initial alarm queue size
    alarm_queue.size = 0;

    
    xTaskCreatePinnedToCore(&alarmMonitor, "alarm-monitor", 4048, NULL, 1, NULL, 1);

    xTaskCreatePinnedToCore(&alarmSpawner, "alarm-spawner", 4048, NULL, 1, NULL, 1);



    alarm_isr_setup();

    init_i2s();

}