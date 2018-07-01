
#include "retrostore.h"
#include "storage.h"
#include "led.h"
#include "utils.h"
#include "version.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define BIT_CHECK_OTA BIT0

#define KEY_VERSION "version"

static const char* TAG = "OTA";

static TaskHandle_t task_handle;
static EventGroupHandle_t event_group;

#define BUFFSIZE 1024

/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };
/*an packet receive buffer*/
/* an image total length*/
static int binary_file_length = 0;

static int fd;

static bool server_http(const char* path)
{
  if (!connect_server(&fd)) {
    LOG("ERROR: Connection failed");
    return false;
  }

  char* header = NULL;
  if (asprintf(&header, "GET %s HTTP/1.1\r\n"
               "Host: %s\r\n"
               "Accept: text/plain,application/octet-stream\r\n"
               "Connection: close\r\n\r\n", path, RETROSTORE_HOST) < 0) {
    return false;
  }
  write(fd, header, strlen(header));
  free(header);
  
  if (!skip_to_body(fd)) {
    return false;
  }

  return true;
}

static void perform_ota(int32_t remote_version)
{
  char* path = NULL;

  set_led(false, false, true, false, false);
  
  if (asprintf(&path, "/card/%d/firmware", RS_RETROCARD_REVISION) < 0) {
    return;
  }
  bool status = server_http(path);
  free(path);
  if (!status) {
    return;
  }

  esp_ota_handle_t update_handle = 0 ;

  ESP_LOGI(TAG, "Performing OTA");

  const esp_partition_t* update_partition =
    esp_ota_get_next_update_partition(NULL);
  ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
           update_partition->subtype, update_partition->address);
  assert(update_partition != NULL);

  ESP_ERROR_CHECK(esp_ota_begin(update_partition,
                                OTA_SIZE_UNKNOWN, &update_handle));

  bool flag = true;

  while (flag) {
    int buff_len = recv(fd, ota_write_data, BUFFSIZE, 0);
    if (buff_len < 0) {
      ESP_LOGE(TAG, "Error: receive data error! errno=%d", errno);
      return;
    } else if (buff_len == 0) {
      flag = false;
      ESP_LOGI(TAG, "Connection closed, all packets received");
      close(fd);
    } else {
      ESP_ERROR_CHECK(esp_ota_write(update_handle,
                                    (const void *)ota_write_data,
                                    buff_len));
      binary_file_length += buff_len;
    }
  }

  ESP_LOGI(TAG, "Firmware size: %d", binary_file_length);

  ESP_ERROR_CHECK(esp_ota_end(update_handle));
  ESP_ERROR_CHECK(esp_ota_set_boot_partition(update_partition));
  storage_set_i32(KEY_VERSION, remote_version);
  ESP_LOGI(TAG, "Restart system");
  esp_restart();
}

static void check_ota()
{
  char* path = NULL;
  
  if (asprintf(&path, "/card/%d/version", RS_RETROCARD_REVISION) < 0) {
    return;
  }

  bool status = server_http(path);
  free(path);

  if (!status) {
    return;
  }

  char buf[30];
  ssize_t size_read = recv(fd, buf, sizeof(buf) - 1, MSG_WAITALL);

  close(fd);
  
  if (size_read < 0) {
    return;
  }

  buf[size_read] = '\0';
  int32_t version_remote;
  if (sscanf(buf, "%d", &version_remote) != 1) {
    return;
  }

  ESP_LOGI(TAG, "Version (remote): %d", version_remote);
  
  bool needs_ota = true;

  if (storage_has_key(KEY_VERSION)) {
    int32_t version_local = storage_get_i32(KEY_VERSION);
    ESP_LOGI(TAG, "Version (local): %d", version_local);
    needs_ota = version_local < version_remote;
  }
  
  if (needs_ota) {
    perform_ota(version_remote);
  }
}

static void ota_task(void* p)
{
  TickType_t delay = portMAX_DELAY;
  
  EventBits_t bits = xEventGroupWaitBits(event_group,
                                         BIT_CHECK_OTA,
                                         pdTRUE,
                                         pdFALSE,
                                         delay);
  if (bits != 0) {
    check_ota();
  }
  vTaskDelete(NULL);
}

void trigger_ota_check()
{
  xEventGroupSetBits(event_group, BIT_CHECK_OTA);
}

void switch_to_factory()
{
  esp_partition_iterator_t pi;

  pi = esp_partition_find(ESP_PARTITION_TYPE_APP,
                          ESP_PARTITION_SUBTYPE_APP_FACTORY,
                          "factory");
  if (pi == NULL) {
    ESP_LOGE(TAG, "Failed to find factory partition");
  } else {
    const esp_partition_t* factory = esp_partition_get(pi);
    esp_partition_iterator_release(pi);
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(factory));
    esp_restart();
  }
}

void init_ota()
{
  event_group = xEventGroupCreate();
  xEventGroupClearBits(event_group, 0xff);
  xTaskCreatePinnedToCore(ota_task, "ota", 4096, NULL, 1, &task_handle, 0);
}
