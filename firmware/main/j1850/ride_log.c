#include "ride_log.h"
#include "ride_log_format.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>  // fsync

static const char *TAG = "ridelog";

// Board microSD wiring (Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C): the card sits
// on SDMMC slot 0 at the fixed IO_MUX pins CLK43/CMD44/D0-3=39..42 (4-bit),
// powered from the P4's internal LDO channel 4. Slot 1 is the C6 radio's SDIO
// link (see mount_card). These are fixed board constants, not user pins.
#define MOUNT_POINT "/sdcard"
#define SD_LDO_CHAN 4
#define SD_PIN_CLK  43
#define SD_PIN_CMD  44
#define SD_PIN_D0   39
#define SD_PIN_D1   40
#define SD_PIN_D2   41
#define SD_PIN_D3   42

// One VPW line worst case is ~150 chars; round up with room for the newline.
#define RL_LINE_MAX 176
// Byte ring between the sniffer task (producer) and the flush task. ~10 s of
// dense traffic; whole lines are dropped (counted) rather than truncated when
// it fills, so a stall never corrupts a line.
#define STREAM_SZ 16384
#define CHUNK_SZ  1024
#define SYNC_US   (1000 * 1000)  // fsync + storage-stat cadence: 1 s

typedef enum { CMD_NONE = 0, CMD_START, CMD_STOP } cmd_t;

static volatile ride_log_state_t s_state = RIDE_LOG_UNINIT;
static volatile cmd_t            s_cmd;
static StreamBufferHandle_t      s_stream;
static FILE                     *s_file;
static sdmmc_card_t             *s_card;
static sd_pwr_ctrl_handle_t      s_pwr;

static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;
static ride_log_stats_t s_stats;  // mirror read by the bench UI, guarded by s_mux

static void set_state(ride_log_state_t st)
{
    portENTER_CRITICAL(&s_mux);
    s_state       = st;
    s_stats.state = st;
    portEXIT_CRITICAL(&s_mux);
}

#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
// The mount reuses the controller esp_hosted already created (see mount_card);
// these stubs replace the driver's init/deinit so it does not try to create a
// second one. Workaround for esp-idf#16233.
static esp_err_t sdmmc_host_init_stub(void)
{
    return ESP_OK;
}
static esp_err_t sdmmc_host_deinit_stub(void)
{
    return ESP_OK;
}
#endif

// Boot-time durability check: log every persisted ride_NNN.log with its byte
// size so a bench power-off test can be verified from the serial log, without
// pulling the card. Runs once, on the first successful mount.
static void log_existing_sessions(void)
{
    DIR *d = opendir(MOUNT_POINT);
    if (!d)
        return;
    int            count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int n = -1;
        if (sscanf(e->d_name, "ride_%d.log", &n) != 1)
            continue;
        char path[48];
        snprintf(path, sizeof(path), MOUNT_POINT "/%s", e->d_name);
        struct stat st;
        long        size = (stat(path, &st) == 0) ? (long)st.st_size : -1;
        ESP_LOGI(TAG, "  session %s: %ld bytes", e->d_name, size);
        count++;
    }
    closedir(d);
    ESP_LOGI(TAG, "existing sessions on card: %d", count);
}

static bool mount_card(void)
{
    if (s_card)
        return true;

    // The P4 feeds SD IO power from an on-chip LDO; bring it up before the host.
    sd_pwr_ctrl_ldo_config_t ldo = {.ldo_chan_id = SD_LDO_CHAN};
    if (!s_pwr && sd_pwr_ctrl_new_on_chip_ldo(&ldo, &s_pwr) != ESP_OK) {
        ESP_LOGE(TAG, "SD LDO init failed");
        return false;
    }

    // The P4 has ONE SD/MMC controller (SDMMC_LL_HOST_CTLR_NUMS == 1) shared by
    // two slots: the C6 radio's SDIO link on slot 1 and this microSD on slot 0.
    // esp_hosted creates and owns that controller very early at boot, and the
    // IDF >= 6.0 driver refuses a second creation ("no available sd host
    // controller"). Mounting on slot 0 with init/deinit stubbed makes the driver
    // reuse the live controller and just attach slot 0, so the ride log and BLE
    // coexist. Mirrors the esp_hosted host_sdcard_with_hosted example.
    sdmmc_host_t host    = SDMMC_HOST_DEFAULT();
    host.slot            = SDMMC_HOST_SLOT_0;
    host.pwr_ctrl_handle = s_pwr;
#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    host.init   = sdmmc_host_init_stub;
    host.deinit = sdmmc_host_deinit_stub;
#endif

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width               = 4;
    slot.clk                 = SD_PIN_CLK;
    slot.cmd                 = SD_PIN_CMD;
    slot.d0                  = SD_PIN_D0;
    slot.d1                  = SD_PIN_D1;
    slot.d2                  = SD_PIN_D2;
    slot.d3                  = SD_PIN_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,  // never reformat the owner's card
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s) - no card?", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }
    ESP_LOGI(TAG, "SD mounted at %s", MOUNT_POINT);
    log_existing_sessions();
    return true;
}

// Next free /sdcard/ride_NNN.log index (highest existing + 1, else 0).
static int next_index(void)
{
    DIR *d = opendir(MOUNT_POINT);
    if (!d)
        return 0;
    int            best = -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int n = -1;
        if (sscanf(e->d_name, "ride_%d.log", &n) == 1 && n > best)
            best = n;
    }
    closedir(d);
    return best + 1;
}

static void update_storage_stats(void)
{
    struct statvfs vfs;
    if (statvfs(MOUNT_POINT, &vfs) != 0)
        return;
    uint64_t total = (uint64_t)vfs.f_blocks * vfs.f_frsize;
    uint64_t avail = (uint64_t)vfs.f_bavail * vfs.f_frsize;
    portENTER_CRITICAL(&s_mux);
    s_stats.mb_total = (uint32_t)(total / (1024 * 1024));
    s_stats.mb_used  = (uint32_t)((total - avail) / (1024 * 1024));
    portEXIT_CRITICAL(&s_mux);
}

static void do_start(void)
{
    if (s_state == RIDE_LOG_RECORDING)
        return;
    if (!mount_card()) {
        set_state(RIDE_LOG_NO_CARD);
        return;
    }
    int  idx = next_index();
    char path[48];
    snprintf(path, sizeof(path), MOUNT_POINT "/ride_%03d.log", idx);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        ESP_LOGE(TAG, "open %s failed", path);
        set_state(RIDE_LOG_ERROR);
        return;
    }
    s_file = fp;
    xStreamBufferReset(s_stream);
    portENTER_CRITICAL(&s_mux);
    s_stats.frames  = 0;
    s_stats.dropped = 0;
    portEXIT_CRITICAL(&s_mux);

    char   hdr[80];
    int    n = ride_log_format_header((uint32_t)idx, esp_timer_get_time() / 1000, hdr, sizeof(hdr));
    size_t hn = (n > 0 && (size_t)n < sizeof(hdr) - 1) ? (size_t)n : sizeof(hdr) - 1;
    hdr[hn++] = '\n';
    fwrite(hdr, 1, hn, fp);

    update_storage_stats();
    set_state(RIDE_LOG_RECORDING);
    ESP_LOGI(TAG, "recording -> %s", path);
}

static void do_stop(void)
{
    if (s_file) {
        char   chunk[CHUNK_SZ];
        size_t got;
        while ((got = xStreamBufferReceive(s_stream, chunk, sizeof(chunk), 0)) > 0)
            fwrite(chunk, 1, got, s_file);
        fflush(s_file);
        fsync(fileno(s_file));
        fclose(s_file);
        s_file = NULL;
        update_storage_stats();
        ESP_LOGI(TAG, "session closed (%lu frames, %lu dropped)", (unsigned long)s_stats.frames,
                 (unsigned long)s_stats.dropped);
    }
    set_state(s_card ? RIDE_LOG_IDLE : RIDE_LOG_NO_CARD);
}

static void flush_task(void *arg)
{
    (void)arg;
    char    chunk[CHUNK_SZ];
    int64_t last_sync = esp_timer_get_time();

    for (;;) {
        cmd_t c = s_cmd;
        s_cmd   = CMD_NONE;
        if (c == CMD_START)
            do_start();
        else if (c == CMD_STOP)
            do_stop();

        size_t got = xStreamBufferReceive(s_stream, chunk, sizeof(chunk), pdMS_TO_TICKS(200));
        if (got > 0 && s_file) {
            if (fwrite(chunk, 1, got, s_file) != got) {
                ESP_LOGE(TAG, "SD write failed - stopping (card full?)");
                fclose(s_file);
                s_file = NULL;
                set_state(RIDE_LOG_ERROR);
                continue;
            }
        }
        int64_t now = esp_timer_get_time();
        if (s_file && now - last_sync > SYNC_US) {
            fflush(s_file);
            fsync(fileno(s_file));
            update_storage_stats();
            last_sync = now;
        }
    }
}

void ride_log_init(void)
{
    s_stream = xStreamBufferCreate(STREAM_SZ, 1);
    if (!s_stream) {
        ESP_LOGE(TAG, "stream buffer alloc failed - ride log disabled");
        set_state(RIDE_LOG_ERROR);
        return;
    }
    set_state(mount_card() ? RIDE_LOG_IDLE : RIDE_LOG_NO_CARD);
    // Low priority: SD writes must yield to the sniffer, producer, and audio.
    // Generous stack — FATFS fopen/fwrite/statvfs/opendir are stack-heavy.
    xTaskCreatePinnedToCore(flush_task, "ridelog", 8192, NULL, 3, NULL, 0);
}

void ride_log_start(void)
{
    s_cmd = CMD_START;
}

void ride_log_stop(void)
{
    s_cmd = CMD_STOP;
}

void ride_log_toggle(void)
{
    s_cmd = (s_state == RIDE_LOG_RECORDING) ? CMD_STOP : CMD_START;
}

void ride_log_frame(const j1850_frame_t *f)
{
    if (s_state != RIDE_LOG_RECORDING)
        return;

    char line[RL_LINE_MAX];
    int  n = ride_log_format_line(f, esp_timer_get_time() / 1000, line, sizeof(line));
    if (n <= 0)
        return;
    size_t len  = ((size_t)n < sizeof(line) - 1) ? (size_t)n : sizeof(line) - 1;
    line[len++] = '\n';

    // Drop the whole line if it won't fit, so a backed-up flush never writes a
    // half line. Counted, non-fatal.
    if (xStreamBufferSpacesAvailable(s_stream) < len) {
        portENTER_CRITICAL(&s_mux);
        s_stats.dropped++;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    xStreamBufferSend(s_stream, line, len, 0);
    portENTER_CRITICAL(&s_mux);
    s_stats.frames++;
    portEXIT_CRITICAL(&s_mux);
}

void ride_log_get_stats(ride_log_stats_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_mux);
}
