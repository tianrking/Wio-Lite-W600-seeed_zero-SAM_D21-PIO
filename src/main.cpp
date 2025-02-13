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
// Function to format a float value into a string with correct decimal representation
void format_float(float value, char* buffer, int buffer_size) {
    int integer_part = (int)value;  // Get the integer part
    float fractional_part = value - integer_part;  // Get the fractional part
    int fractional_int = (int)(fractional_part * 100);  // Convert fractional part to an integer (2 decimal places)

    // Format the integer and fractional part as a string with a dot in between
    snprintf(buffer, buffer_size, "%d.%02d", integer_part, fractional_int);
}

void lvglTask(void *pvParameters) {
    // Initialize LVGL *within* the task.
    lv_init();

    #if USE_LV_LOG != 0
        lv_log_register_print_cb(my_print);
    #endif

    if (xSemaphoreTake(tftMutex, portMAX_DELAY) == pdTRUE) {
        tft.begin();
        tft.setRotation(3);
        xSemaphoreGive(tftMutex);
    }

    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX / 40); // 1/40th buffer

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;  // Or LV_INDEV_TYPE_POINTER
    indev_drv.read_cb = read_encoder;
    lv_indev_drv_register(&indev_drv);

    // --- Stock Data and Display Setup ---
    struct StockData {
        const char* name;
        lv_obj_t* name_label;
        lv_obj_t* price_label;
        lv_obj_t* change_label;
        lv_obj_t* container;
        lv_obj_t* dynamic_price_label;
        lv_obj_t* dynamic_change_label;
        float value;
        float prev_value;
        float min_val;
        float max_val;
    };

    StockData stocks[] = {
        {"Apple", NULL, NULL, NULL, NULL, NULL, NULL, 0.0f, 0.0f, 150.0f, 180.0f},
        {"Tesla", NULL, NULL, NULL, NULL, NULL, NULL, 0.0f, 0.0f, 600.0f, 900.0f},
        {"Alibaba", NULL, NULL, NULL, NULL, NULL, NULL, 0.0f, 0.0f, 80.0f, 120.0f},
        {"Meta", NULL, NULL, NULL, NULL, NULL, NULL, 0.0f, 0.0f, 250.0f, 350.0f}
    };
    const int num_stocks = sizeof(stocks) / sizeof(stocks[0]);

    // Create containers and labels for each stock *within* the mutex
    if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
        int screen_width = lv_disp_get_hor_res(NULL);
        int screen_height = lv_disp_get_ver_res(NULL);
        int container_width = screen_width / 2;
        int container_height = screen_height / 2;
        int label_height = 20; // Approximate height of each label

        for (int i = 0; i < num_stocks; i++) {
            // 1. Create containers
            stocks[i].container = lv_obj_create(lv_scr_act(), NULL);
            lv_obj_set_size(stocks[i].container, container_width, container_height);
            lv_obj_set_style_local_bg_color(stocks[i].container, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xD3D3D3)); // Light Gray
            lv_obj_set_style_local_border_color(stocks[i].container, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE); // White border
            lv_obj_set_style_local_border_width(stocks[i].container, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);  // Border width
            lv_obj_set_style_local_border_opa(stocks[i].container, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER); // Make border fully opaque

            // 2. Position containers
            if (i == 0) {
                lv_obj_align(stocks[i].container, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);
            } else if (i == 1) {
                lv_obj_align(stocks[i].container, NULL, LV_ALIGN_IN_TOP_RIGHT, 0, 0);
            } else if (i == 2) {
                lv_obj_align(stocks[i].container, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
            } else {
                lv_obj_align(stocks[i].container, NULL, LV_ALIGN_IN_BOTTOM_RIGHT, 0, 0);
            }

            // 3. Create labels *inside* the container
            stocks[i].name_label = lv_label_create(stocks[i].container, NULL);
            lv_obj_align(stocks[i].name_label, NULL, LV_ALIGN_IN_TOP_LEFT, 5, 5);
            lv_label_set_text(stocks[i].name_label, stocks[i].name);
            lv_obj_set_style_local_text_color(stocks[i].name_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

            stocks[i].price_label = lv_label_create(stocks[i].container, NULL);
            lv_obj_align(stocks[i].price_label, NULL, LV_ALIGN_IN_TOP_LEFT, 5, 5 + label_height);
            lv_label_set_text(stocks[i].price_label, "Price: ");
            lv_obj_set_style_local_text_color(stocks[i].price_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

            stocks[i].change_label = lv_label_create(stocks[i].container, NULL);
            lv_obj_align(stocks[i].change_label, NULL, LV_ALIGN_IN_TOP_LEFT, 5, 5 + 2 * label_height);
            lv_label_set_text(stocks[i].change_label, "Change: ");
            lv_obj_set_style_local_text_color(stocks[i].change_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

            stocks[i].dynamic_price_label = lv_label_create(stocks[i].container, NULL);
            lv_obj_align(stocks[i].dynamic_price_label, stocks[i].price_label, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
            lv_label_set_text(stocks[i].dynamic_price_label, "");

            stocks[i].dynamic_change_label = lv_label_create(stocks[i].container, NULL);
            lv_obj_align(stocks[i].dynamic_change_label, stocks[i].change_label, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
            lv_label_set_text(stocks[i].dynamic_change_label, "");

            stocks[i].prev_value = stocks[i].min_val;
        }
        xSemaphoreGive(lvglMutex);
    }

    auto update_stock_values = [&]() {
        for (int i = 0; i < num_stocks; i++) {
            stocks[i].prev_value = stocks[i].value;
            float range = stocks[i].max_val - stocks[i].min_val;
            stocks[i].value = stocks[i].min_val + (range * ((float)rand() / (float)RAND_MAX));
        }
    };

    unsigned long last_update = 0;
    srand(millis());

    while (1) {
        if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(lvglMutex);
        }

        if (millis() - last_update >= 1000) {
            last_update = millis();
            update_stock_values();

            if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                for (int i = 0; i < num_stocks; i++) {
                    float change = (stocks[i].value - stocks[i].prev_value) / stocks[i].prev_value * 100.0f;

                    lv_color_t price_change_color = (change >= 0) ? LV_COLOR_RED : LV_COLOR_GREEN;
                    lv_color_t change_color = (change >= 0) ? LV_COLOR_GREEN : LV_COLOR_RED;

                    // Update labels using the new format_float function
                    char price_str[20];
                    char change_str[20];

                    format_float(stocks[i].value, price_str, sizeof(price_str));
                    format_float(change, change_str, sizeof(change_str));

                    lv_label_set_text(stocks[i].dynamic_price_label, price_str);
                    lv_label_set_text(stocks[i].dynamic_change_label, change_str);

                    lv_obj_set_style_local_text_color(stocks[i].dynamic_price_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, price_change_color);
                    lv_obj_set_style_local_text_color(stocks[i].dynamic_change_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, change_color);
                }
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