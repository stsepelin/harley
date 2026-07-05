#include "sound.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>

static const char *TAG = "sound";

// I2S/codec format. Mono 22050 Hz matches the BSP's default I2S config
// (BSP_I2S_DUPLEX_MONO_CFG(22050) in esp32_p4_wifi6_touch_lcd_xc.c) so we
// avoid forcing a reconfigure.
#define SAMPLE_RATE     22050
#define BITS_PER_SAMPLE 16
#define CHANNELS        1

// Click sound: 30 ms — long enough to feel like a relay tick, short
// enough that a 1.5 Hz blink (~660 ms period) leaves plenty of silence.
#define CLICK_MS        30
#define CLICK_SAMPLES   ((SAMPLE_RATE * CLICK_MS) / 1000)

// Worker task: pinned to core 0 alongside the sim. Sim wakes for ~5 ms
// every 50 ms and is otherwise idle, so the audio task has a quiet
// neighbour. The alternative — core 1 — has LVGL's render pthread on
// it, which would delay our codec write whenever the gauge has a heavy
// frame (turn signals + shift-light + tach all redrawing at once).
#define AUDIO_TASK_STACK   4096
#define AUDIO_TASK_PRIO    3
#define AUDIO_TASK_CORE    0
#define AUDIO_QUEUE_DEPTH  4

typedef enum {
    SND_TURN_CLICK,
} sound_event_t;

static esp_codec_dev_handle_t s_codec;
static QueueHandle_t          s_queue;
static int16_t                s_click[CLICK_SAMPLES];
static bool                   s_enabled = true;

// Synthesise a short percussive click — two sinusoids (1200 + 2400 Hz)
// under an exponential decay envelope. Pure software so we don't ship a
// WAV asset just to play a 30 ms tick. Tweak frequencies/decay to taste.
static void synth_click(int16_t *out, size_t n)
{
    const float decay     = 80.0f;          // tau ≈ 12.5 ms
    const float two_pi    = 6.283185307f;
    const float low_freq  = 1200.0f;
    const float high_freq = 2400.0f;

    for (size_t i = 0; i < n; i++) {
        float t   = (float)i / (float)SAMPLE_RATE;
        float env = expf(-t * decay);
        float s   = env * (0.65f * sinf(two_pi * low_freq  * t) +
                           0.25f * sinf(two_pi * high_freq * t));
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = (int16_t)(s * 25000.0f);   // ~76 % of full scale headroom
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    sound_event_t evt;
    while (1) {
        if (xQueueReceive(s_queue, &evt, portMAX_DELAY) != pdTRUE) continue;
        switch (evt) {
        case SND_TURN_CLICK:
            esp_codec_dev_write(s_codec, s_click, sizeof(s_click));
            break;
        }
    }
}

void sound_init(void)
{
    if (s_codec) return;     // idempotent

    s_codec = bsp_audio_codec_speaker_init();
    if (!s_codec) {
        ESP_LOGE(TAG, "speaker init failed; sound disabled");
        return;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = SAMPLE_RATE,
        .channel         = CHANNELS,
        .bits_per_sample = BITS_PER_SAMPLE,
    };
    if (esp_codec_dev_open(s_codec, &fs) != 0) {
        ESP_LOGE(TAG, "codec open failed; sound disabled");
        s_codec = NULL;
        return;
    }
    esp_codec_dev_set_out_vol(s_codec, 70);

    synth_click(s_click, CLICK_SAMPLES);

    s_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(sound_event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "queue create failed; sound disabled");
        s_codec = NULL;
        return;
    }
    xTaskCreatePinnedToCore(audio_task, "audio", AUDIO_TASK_STACK, NULL,
                            AUDIO_TASK_PRIO, NULL, AUDIO_TASK_CORE);
}

void sound_set_enabled(bool enabled)
{
    s_enabled = enabled;
}

void sound_set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (!s_codec) return;
    esp_codec_dev_set_out_vol(s_codec, pct);
}

void sound_play_turn_click(void)
{
    if (!s_queue || !s_enabled) return;
    sound_event_t evt = SND_TURN_CLICK;
    // Non-blocking enqueue — drop if queue is full so a rapid double-click
    // can't stall the caller (sim task). The next blink edge will get a
    // fresh chance.
    xQueueSend(s_queue, &evt, 0);
}
