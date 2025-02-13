#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Seeed_Arduino_FreeRTOS.h> // Include FreeRTOS

// Use a lower LVGL tick period.  5ms is often too fast.  20ms is usually fine.
#define LVGL_TICK_PERIOD 20

// Select the serial port
#define Terminal Serial

// Task Handles
TaskHandle_t lvglTaskHandle;
TaskHandle_t monitorTaskHandle;


// TFT LCD object (make sure this is still global)
TFT_eSPI tft = TFT_eSPI();

// Mutex to protect TFT and LVGL access (important!)
SemaphoreHandle_t tftMutex;
SemaphoreHandle_t lvglMutex;  // Separate mutex for LVGL


static lv_disp_buf_t disp_buf;
// Reduce buffer size for LVGL 7.  1/10th of the screen is often too large.
static lv_color_t buf[LV_HOR_RES_MAX * LV_VER_RES_MAX / 20]; //  1/20th of the screen

#if USE_LV_LOG != 0
/* Serial debugging */
void my_print(lv_log_level_t level, const char * file, uint32_t line, const char * dsc)
{
  Serial.printf("%s@%d->%s\r\n", file, line, dsc);
  // No delay here!  Delays in the print function can cause problems.
}
#endif

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  if (xSemaphoreTake(tftMutex, portMAX_DELAY) == pdTRUE) { // Protect TFT access
    uint16_t c;
    tft.startWrite(); /* Start new TFT transaction */
    tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1)); /* set the working window */
    for (int y = area->y1; y <= area->y2; y++) {
      for (int x = area->x1; x <= area->x2; x++) {
        c = color_p->full;
        tft.writeColor(c, 1);
        color_p++;
      }
    }
    tft.endWrite(); /* terminate TFT transaction */
    xSemaphoreGive(tftMutex); // Release mutex
  }
  lv_disp_flush_ready(disp); /* tell lvgl that flushing is done */
}



/* Interrupt driven periodic handler */
// This is now a FreeRTOS task, not a simple function
static void lv_tick_task(void *pvParameters) {
  while (1) {
    lv_tick_inc(LVGL_TICK_PERIOD);
    vTaskDelay(pdMS_TO_TICKS(LVGL_TICK_PERIOD)); // Use FreeRTOS delay
  }
}


/* Reading input device (simulated encoder here) */
bool read_encoder(lv_indev_drv_t * indev, lv_indev_data_t * data)
{
  static int32_t last_diff = 0;
  int32_t diff = 0; /* Dummy - no movement */
  int btn_state = LV_INDEV_STATE_REL; /* Dummy - no press */

  data->enc_diff = diff - last_diff;;
  data->state = btn_state;

  last_diff = diff;

  return false;
}

// No longer global.  Created inside the lvglTask.
// lv_obj_t * time_label;


//*****************************************************************
// LVGL Task: Handles all LVGL related operations
//*****************************************************************
void lvglTask(void *pvParameters) {

    // Initialize LVGL *within* the task, after the mutex is created.
    lv_init();

    #if USE_LV_LOG != 0
        lv_log_register_print_cb(my_print); /* register print function for debugging */
    #endif


    if (xSemaphoreTake(tftMutex, portMAX_DELAY) == pdTRUE) {
        tft.begin();
        tft.setRotation(3);
        xSemaphoreGive(tftMutex);
    }

    // Use a reasonable buffer size. 1/10th or 1/20th of the screen is common.
    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX / 20);

    /*Initialize the display*/
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /*Initialize the input device driver*/
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;  // Or LV_INDEV_TYPE_POINTER, etc.
    indev_drv.read_cb = read_encoder;
    lv_indev_drv_register(&indev_drv);


    // --- Time Display Setup ---
    lv_obj_t * time_label = NULL; // Declare the label pointer

    if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
        time_label = lv_label_create(lv_scr_act(), NULL); // Create the label, parent is screen
        lv_obj_align(time_label, NULL, LV_ALIGN_CENTER, 0, 0); // Center the label
        lv_label_set_text(time_label, "00:00:00"); // Initial time value
        xSemaphoreGive(lvglMutex);
    }


    unsigned long last_update = 0;

    while (1) {
        // Handle LVGL tasks.
        if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(lvglMutex);
        }

        // Update the time every second
        if (millis() - last_update >= 1000) {
            last_update = millis();

            // Get the current time (using FreeRTOS's xTaskGetTickCount() for consistency)
            TickType_t ticks = xTaskGetTickCount();
            unsigned long milliseconds = (ticks * portTICK_PERIOD_MS); // Convert to milliseconds
            unsigned long seconds = milliseconds / 1000;
            unsigned long h = (seconds / 3600) % 24; // Modulo 24 for hours (0-23)
            unsigned long m = (seconds % 3600) / 60;
            unsigned long s = seconds % 60;


            // Format the time string
            char time_str[9]; // "hh:mm:ss\0"
            sprintf(time_str, "%02lu:%02lu:%02lu", h, m, s);

            // Update the label text (INSIDE the mutex)
            if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                lv_label_set_text(time_label, time_str);
                xSemaphoreGive(lvglMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Yield
    }
}
//*****************************************************************
//  Task Monitor: Prints task stack information (Optional, but useful)
//*****************************************************************
void taskMonitor(void* pvParameters) {
    while (1) {
        Terminal.println("");
        Terminal.println("******************************");
        Terminal.println("[Stacks Free Bytes Remaining] ");

        Terminal.print("LVGL Task: ");
        Terminal.println(uxTaskGetStackHighWaterMark(lvglTaskHandle));

        Terminal.print("Monitor Task: ");
        Terminal.println(uxTaskGetStackHighWaterMark(NULL)); // or monitorTaskHandle

        Terminal.println("******************************");
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    }
}

//*****************************************************************
// Setup function
//*****************************************************************
void setup() {
  Terminal.begin(115200);
//   while (!Terminal);
  delay(1000);

  Terminal.println("");
  Terminal.println("******************************");
  Terminal.println("        Program start         ");
  Terminal.println("******************************");

    // Create mutexes *before* creating tasks.
    tftMutex = xSemaphoreCreateMutex();
    if (tftMutex == NULL) {
        Terminal.println("Failed to create TFT mutex!");
        while (1); // Fatal error
    }

    lvglMutex = xSemaphoreCreateMutex();  // Create LVGL mutex
    if (lvglMutex == NULL) {
        Terminal.println("Failed to create LVGL mutex!");
        while (1);
    }

  // Create LVGL tick task (essential for LVGL timing)
  xTaskCreate(lv_tick_task, "LVGL Tick", 128, NULL, tskIDLE_PRIORITY + 1, NULL);

  // Create the main LVGL task
  xTaskCreate(lvglTask, "LVGL Task", 4096, NULL, tskIDLE_PRIORITY + 2, &lvglTaskHandle);  // Give LVGL a good size stack!

  // Create the monitor task (optional, but useful for debugging)
  xTaskCreate(taskMonitor, "Task Monitor", 256, NULL, tskIDLE_PRIORITY + 1, &monitorTaskHandle);



  // Start the RTOS scheduler
  vTaskStartScheduler();

  // Should never reach here
  while (1);
}

void loop() {
    // Empty.  Everything is handled by FreeRTOS tasks.
}