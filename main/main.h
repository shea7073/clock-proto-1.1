#ifndef MAIN_H
#define MAIN_H


typedef struct {
    char riff[4];                // "RIFF"
    uint32_t chunk_size;         // Chunk size
    char wave[4];                // "WAVE"
    char fmt[4];                 // "fmt "
    uint32_t subchunk1_size;     // Size of the fmt chunk
    uint16_t audio_format;       // Audio format 1=PCM
    uint16_t num_channels;       // Number of channels
    uint32_t sample_rate;        // Sampling Frequency in Hz
    uint32_t byte_rate;          // bytes per second
    uint16_t block_align;        // 2=16-bit mono, 4=16-bit stereo
    uint16_t bits_per_sample;    // Number of bits per sample
    char subchunk2_id[4];        // "data"
    uint32_t subchunk2_size;     // Sampled data length
} wav_header_t;

void play_wav(int16_t *, FILE*);
int16_t * allocate_i2s_buffer();
FILE* open_wav(char*);
void init_display_gpio(void);
void start_mdns();
static esp_err_t on_default_url(httpd_req_t *req);
static esp_err_t on_set_alarm(httpd_req_t *req);
static esp_err_t on_get_alarms(httpd_req_t *req);
static esp_err_t on_delete_alarm(httpd_req_t *req);
static esp_err_t on_toggle_alarm(httpd_req_t *req);
static void init_server();
void mount_sdcard(void);
void displayTime(void * params);
void alarm_triggered_task(void * params);
void alarmMonitor(void * params);
void alarmSpawner(void * params);

void buttonPushedTask(void *params);
void alarm_isr_setup();
void init_i2s();

#endif