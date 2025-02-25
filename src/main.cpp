#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Seeed_Arduino_FreeRTOS.h>
#include "LIS3DHTR.h" // Include accelerometer library

// LVGL tick period
#define LVGL_TICK_PERIOD 20

// Select the serial port
#define Terminal Serial

// Task Handles
TaskHandle_t lvglTaskHandle;
TaskHandle_t monitorTaskHandle;
TaskHandle_t sensorTaskHandle;
TaskHandle_t buttonTaskHandle; // Task for handling button presses

// TFT LCD object
TFT_eSPI tft = TFT_eSPI();

// Mutexes
SemaphoreHandle_t tftMutex;
SemaphoreHandle_t lvglMutex;
SemaphoreHandle_t sensorDataMutex;

// Sensor object
LIS3DHTR<TwoWire> lis;

// Data storage for the chart (circular buffer)
#define MAX_SIZE 50
float x_data[MAX_SIZE];
float y_data[MAX_SIZE];
float z_data[MAX_SIZE];
int data_index = 0; // Current index for circular buffer

// Chart range
int16_t chart_range_y = 200;  // Initial Y-axis range
int16_t chart_range_x = MAX_SIZE; // Initial x- axis range

static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * LV_VER_RES_MAX / 20];

// Background colors
lv_color_t bg_colors[] = {
    LV_COLOR_BLACK, LV_COLOR_WHITE, LV_COLOR_RED, LV_COLOR_GREEN, LV_COLOR_BLUE,
    LV_COLOR_YELLOW, LV_COLOR_CYAN, LV_COLOR_MAGENTA, LV_COLOR_GRAY,
    LV_COLOR_SILVER, LV_COLOR_MAROON, LV_COLOR_OLIVE, LV_COLOR_LIME,
    LV_COLOR_AQUA, LV_COLOR_TEAL, LV_COLOR_NAVY, LV_COLOR_PURPLE,
};
int bg_color_index = 0;
int num_bg_colors = sizeof(bg_colors) / sizeof(bg_colors[0]);

#if USE_LV_LOG != 0
/* Serial debugging */
void my_print(lv_log_level_t level, const char * file, uint32_t line, const char * dsc) {
    Serial.printf("%s@%d->%s\r\n", file, line, dsc);
}
#endif

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    if (xSemaphoreTake(tftMutex, portMAX_DELAY) == pdTRUE) {
        uint16_t c;
        tft.startWrite();
        tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1));
        for (int y = area->y1; y <= area->y2; y++) {
            for (int x = area->x1; x <= area->x2; x++) {
                c = color_p->full;
                tft.writeColor(c, 1);
                color_p++;
            }
        }
        tft.endWrite();
        xSemaphoreGive(tftMutex);
    }
    lv_disp_flush_ready(disp);
}

/* Interrupt driven periodic handler */
static void lv_tick_task(void *pvParameters) {
    while (1) {
        lv_tick_inc(LVGL_TICK_PERIOD);
        vTaskDelay(pdMS_TO_TICKS(LVGL_TICK_PERIOD));
    }
}

/* Reading input device (simulated encoder) */
bool read_encoder(lv_indev_drv_t * indev, lv_indev_data_t * data) {
    static int32_t last_diff = 0;
    int32_t diff = 0;
    int btn_state = LV_INDEV_STATE_REL;
    data->enc_diff = diff - last_diff;;
    data->state = btn_state;
    last_diff = diff;
    return false;
}

// Chart object and series
lv_obj_t * chart;
lv_chart_series_t * ser1;
lv_chart_series_t * ser2;
lv_chart_series_t * ser3;

//*****************************************************************
// Button Task: Handles button presses (center, up, down, left, right)
//*****************************************************************
void buttonTask(void *pvParameters) {
    pinMode(WIO_5S_PRESS, INPUT_PULLUP);
    pinMode(WIO_5S_UP, INPUT_PULLUP);
    pinMode(WIO_5S_DOWN, INPUT_PULLUP);
    pinMode(WIO_5S_LEFT, INPUT_PULLUP);
    pinMode(WIO_5S_RIGHT, INPUT_PULLUP);

    bool last_center_state = HIGH;

    while (1) {
        bool current_center_state = digitalRead(WIO_5S_PRESS);
        bool up_state = digitalRead(WIO_5S_UP);
        bool down_state = digitalRead(WIO_5S_DOWN);
        bool left_state = digitalRead(WIO_5S_LEFT);
        bool right_state = digitalRead(WIO_5S_RIGHT);

        // Center button: Change background color
        if (current_center_state == LOW && last_center_state == HIGH) {
            if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                bg_color_index = (bg_color_index + 1) % num_bg_colors;
                lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, bg_colors[bg_color_index]);
                lv_obj_set_style_local_bg_opa(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
                xSemaphoreGive(lvglMutex);
            }
        }
        last_center_state = current_center_state;

        // Up/Down buttons: Zoom Y-axis
        if (up_state == LOW) {
            if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                chart_range_y -= 10; // Zoom in
                if (chart_range_y < 20) chart_range_y = 20; // Limit zoom
                lv_chart_set_range(chart, -chart_range_y, chart_range_y);
                xSemaphoreGive(lvglMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
        } else if (down_state == LOW) {
            if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                chart_range_y += 10; // Zoom out
                if (chart_range_y > 500) chart_range_y = 500; // Limit zoom
                lv_chart_set_range(chart, -chart_range_y, chart_range_y);
                xSemaphoreGive(lvglMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(50)); //Debounce
        }
        // Left/Right buttons: Zoom X-axis
          if (left_state == LOW) {
                if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                    chart_range_x -= 2; // Zoom in (show fewer points)
                    if (chart_range_x < 5) chart_range_x = 5; // Limit zoom to minimum 5 data points
                    lv_chart_set_point_count(chart, chart_range_x);
                      xSemaphoreGive(lvglMutex);
                }
                  vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
          } else if (right_state == LOW) {
                if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                     chart_range_x += 2; // Zoom out (show more points)
                    if (chart_range_x > MAX_SIZE) chart_range_x = MAX_SIZE; // Limit zoom to maximum data points
                    lv_chart_set_point_count(chart, chart_range_x);
                    xSemaphoreGive(lvglMutex);
                }
                 vTaskDelay(pdMS_TO_TICKS(50)); //Debounce
          }

        vTaskDelay(pdMS_TO_TICKS(50)); // General debounce delay
    }
}

//*****************************************************************
// Sensor Task: Reads accelerometer data and updates circular buffers
//*****************************************************************
void sensorTask(void *pvParameters) {
    while (1) {
        // Read sensor data
        float x_raw = lis.getAccelerationX();
        float y_raw = lis.getAccelerationY();
        float z_raw = lis.getAccelerationZ();

        // Protect data access with mutex
        if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
            // Update circular buffers
            x_data[data_index] = x_raw;
            y_data[data_index] = y_raw;
            z_data[data_index] = z_raw;

            data_index = (data_index + 1) % MAX_SIZE; // Circular buffer index update

            xSemaphoreGive(sensorDataMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Sample every 100ms
    }
}

//*****************************************************************
// LVGL Task
//*****************************************************************
void lvglTask(void *pvParameters) {
    lv_init();

#if USE_LV_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    if (xSemaphoreTake(tftMutex, portMAX_DELAY) == pdTRUE) {
        tft.begin();
        tft.setRotation(3);
        xSemaphoreGive(tftMutex);
    }

    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX / 20);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = read_encoder;
    lv_indev_drv_register(&indev_drv);

    // Create the chart *within* the LVGL task
    if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
        chart = lv_chart_create(lv_scr_act(), NULL);
        lv_obj_set_size(chart, 280, 200);
        lv_obj_align(chart, NULL, LV_ALIGN_CENTER, 0, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_range(chart, -chart_range_y, chart_range_y); // Initial range
        lv_chart_set_point_count(chart, chart_range_x); // Initial point count

        // Add three data series
        ser1 = lv_chart_add_series(chart, LV_COLOR_BLUE);
        ser2 = lv_chart_add_series(chart, LV_COLOR_RED);
        ser3 = lv_chart_add_series(chart, LV_COLOR_GREEN);

        xSemaphoreGive(lvglMutex);
    }

    // Set initial background color
    if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
        lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, bg_colors[bg_color_index]);
        lv_obj_set_style_local_bg_opa(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
        xSemaphoreGive(lvglMutex);
    }

    while (1) {
        if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(lvglMutex);
        }

        // Update chart data (protected by both mutexes)
        if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
          if (xSemaphoreTake(lvglMutex, portMAX_DELAY) == pdTRUE) {
                // Update chart series with data from circular buffers
                // Because we are changing point count dynamically we can't pre-fill all values
                // We add points one-by-one until chart is filled.
                lv_chart_clear_serie(chart, ser1);  // Clear previous points
                lv_chart_clear_serie(chart, ser2);
                lv_chart_clear_serie(chart, ser3);
                for (int i = 0; i < chart_range_x; i++) {
                  int buffer_index = (data_index - chart_range_x + i + MAX_SIZE) % MAX_SIZE; // Calculate correct index
                  lv_chart_set_next(chart, ser1, (lv_coord_t)(x_data[buffer_index] * 100)); // Scale for display
                  lv_chart_set_next(chart, ser2, (lv_coord_t)(y_data[buffer_index] * 100));
                  lv_chart_set_next(chart, ser3, (lv_coord_t)(z_data[buffer_index] * 100));
              }
              lv_chart_refresh(chart);
              xSemaphoreGive(lvglMutex);
          }
            xSemaphoreGive(sensorDataMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Update chart
    }
}

//*****************************************************************
//  Task Monitor
//*****************************************************************
void taskMonitor(void* pvParameters) {
    while (1) {
        Terminal.println("");
        Terminal.println("******************************");
        Terminal.println("[Stacks Free Bytes Remaining] ");
        Terminal.print("LVGL Task: ");
        Terminal.println(uxTaskGetStackHighWaterMark(lvglTaskHandle));
        Terminal.print("Sensor Task: ");
        Terminal.println(uxTaskGetStackHighWaterMark(sensorTaskHandle));
        Terminal.print("Button Task: ");
        Terminal.println(uxTaskGetStackHighWaterMark(buttonTaskHandle));
        Terminal.print("Monitor Task: ");
        Terminal.println(uxTaskGetStackHighWaterMark(NULL));
        Terminal.println("******************************");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

//*****************************************************************
// Setup function
//*****************************************************************
void setup() {
    Terminal.begin(115200);
    while (!Terminal);
    delay(1000);
    Terminal.println("Program start");

    // Initialize I2C for the sensor
    Wire1.begin();
    lis.begin(Wire1);
    lis.setOutputDataRate(LIS3DHTR_DATARATE_10HZ); // Slower data rate
    lis.setFullScaleRange(LIS3DHTR_RANGE_2G);

    // Create mutexes
    tftMutex = xSemaphoreCreateMutex();
    if (tftMutex == NULL) {
        Terminal.println("Failed to create TFT mutex!");
        while (1);
    }
    lvglMutex = xSemaphoreCreateMutex();
    if (lvglMutex == NULL) {
        Terminal.println("Failed to create LVGL mutex!");
        while (1);
    }
    sensorDataMutex = xSemaphoreCreateMutex();
    if (sensorDataMutex == NULL) {
        Terminal.println("Failed to create sensor data mutex!");
        while (1);
    }

    // Create tasks
    xTaskCreate(lv_tick_task, "LVGL Tick", 128, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(sensorTask, "Sensor Task", 2048, NULL, tskIDLE_PRIORITY + 3, &sensorTaskHandle);
    xTaskCreate(lvglTask, "LVGL Task", 4096, NULL, tskIDLE_PRIORITY + 2, &lvglTaskHandle);
    // Create the button task
    xTaskCreate(buttonTask, "Button Task", 2048, NULL, tskIDLE_PRIORITY + 2, &buttonTaskHandle);
    xTaskCreate(taskMonitor, "Task Monitor", 256, NULL, tskIDLE_PRIORITY + 1, &monitorTaskHandle);

    // Start the RTOS scheduler
    vTaskStartScheduler();

    while (1); // Should never reach here
}

void loop() {
    // Empty.
}