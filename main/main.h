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


#endif