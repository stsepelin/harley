#include "sound.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
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

// POI alert chirp: 220 ms total — two 80 ms tones at different pitches
// with a 60 ms gap. Distinct from a single click so a rider can ID it
// without looking, but short enough to not be a nuisance.
#define ALERT_MS        220
#define ALERT_SAMPLES   ((SAMPLE_RATE * ALERT_MS) / 1000)

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
    SND_POI_ALERT,
} sound_event_t;

static esp_codec_dev_handle_t s_codec;
static QueueHandle_t          s_queue;
static int16_t                s_click[CLICK_SAMPLES];
// 9.7 KB alert chirp lives in PSRAM, not internal DRAM — at boot ESP-Hosted
// + the early FreeRTOS heap already eat almost all of the first DRAM region,
// and a 10 KB static was enough to push the main task TCB+stack allocation
// over the edge (xTaskCreatePinnedToCore returns pdFAIL, app_startup.c:83
// asserts before app_main runs). I2S DMA throttles codec writes by sample
// rate, so PSRAM latency on the read side is invisible.
static int16_t               *s_alert;
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

// Two-tone attention chirp. First note (880 Hz / A5) → silence → second
// note (1175 Hz / D6). A short attack/release ramp on each tone avoids
// the click that a hard-edged sine cut produces. Pitch interval (fourth
// up) is high enough to stand out over road noise without sounding
// alarming.
static void synth_alert(int16_t *out, size_t n)
{
    const float two_pi    = 6.283185307f;
    const float f1        = 880.0f;
    const float f2        = 1175.0f;
    const size_t tone_n   = (SAMPLE_RATE *  80) / 1000;
    const size_t gap_n    = (SAMPLE_RATE *  60) / 1000;
    const size_t ramp_n   = (SAMPLE_RATE *   8) / 1000;

    for (size_t i = 0; i < n; i++) {
        float s = 0.0f;
        size_t tone_start = 0;
        float  freq       = f1;
        bool   in_tone    = false;

        if (i < tone_n) {
            in_tone   = true;
            tone_start = 0;
            freq      = f1;
        } else if (i < tone_n + gap_n) {
            in_tone = false;
        } else if (i < 2 * tone_n + gap_n) {
            in_tone    = true;
            tone_start = tone_n + gap_n;
            freq       = f2;
        }

        if (in_tone) {
            size_t k = i - tone_start;
            float env = 1.0f;
            if (k < ramp_n)              env = (float)k / (float)ramp_n;
            else if (k > tone_n - ramp_n) env = (float)(tone_n - k) / (float)ramp_n;
            float t = (float)k / (float)SAMPLE_RATE;
            s = env * sinf(two_pi * freq * t) * 0.7f;
        }
        out[i] = (int16_t)(s * 25000.0f);
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
        case SND_POI_ALERT:
            if (s_alert) {
                esp_codec_dev_write(s_codec, s_alert,
                                    ALERT_SAMPLES * sizeof(*s_alert));
            }
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

    s_alert = heap_caps_malloc(ALERT_SAMPLES * sizeof(*s_alert),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_alert) {
        synth_alert(s_alert, ALERT_SAMPLES);
    } else {
        ESP_LOGW(TAG, "alert buffer alloc failed; POI chirp disabled");
    }

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

void sound_play_poi_alert(void)
{
    if (!s_queue || !s_enabled) return;
    sound_event_t evt = SND_POI_ALERT;
    xQueueSend(s_queue, &evt, 0);
}
