# Touch Sensor FSM

The ESP32 touch sensor provides a convenient way to detect touch events. However, in environments with strong interference, the hardware judgment logic may fail. This component uses software solutions to provide a more flexible and reliable solution in such environments.

This component provides a finite state machine (FSM) for managing touch sensor data and states. It includes functions for creating, deleting, updating, and controlling the FSM, as well as handling events.

## Application Drivers Depends on this FSM

- [touch_proximity_sensor](https://components.espressif.com/components/espressif/touch_proximity_sensor)
- [touch_button_sensor](https://components.espressif.com/components/espressif/touch_button_sensor)

## Configuration

The FSM is configured using the `fsm_config_t` structure. Here are the key configuration parameters:

Default configuration can be initialized using:

```c
fsm_config_t config = DEFAULTS_TOUCH_SENSOR_FSM_CONFIG();
```

### Per-Channel Configuration

Several parameters can be configured per channel:

- `threshold_p`: Positive threshold for touch detection (required)
- `threshold_n`: Negative threshold for touch detection (optional)
- `noise_p`: Positive noise threshold for baseline validation (required)
- `noise_n`: Negative noise threshold for baseline validation (required)

Example of setting per-channel thresholds:

```c
float thresholds_p[] = {0.015, 0.01, 0.012}; // Required positive thresholds
float noise_p[] = {0.005, 0.004, 0.003};     // Required positive noise thresholds
float noise_n[] = {0.005, 0.004, 0.003};     // Required negative noise thresholds

config.threshold_p = thresholds_p;
config.noise_p = noise_p;
config.noise_n = noise_n;
```

## States

The FSM has three possible states:
```c
typedef enum {
    FSM_STATE_INACTIVE = 0,  // Touch sensor is inactive
    FSM_STATE_ACTIVE,        // Touch sensor is active
    FSM_STATE_DEBOUNCE,      // Touch sensor is in debounce state
} fsm_state_t;
```

## Data Types

When getting data from the FSM, three types of data are available:
```c
typedef enum {
    FSM_DATA_RAW = 0,    // Raw touch sensor data
    FSM_DATA_SMOOTH,     // Smoothed touch sensor data
    FSM_DATA_BASELINE,   // Baseline touch sensor data
    FSM_DATA_MAX,
} fsm_data_t;
```

## Working Modes

### Polling Mode (`FSM_MODE_POLLING`)
In this mode, the FSM automatically polls touch sensor data using the registered callback function. The polling interval is configurable.

Example:
```c
void polling_callback(fsm_handle_t handle, uint32_t channel, uint32_t *raw_data, void *user_data) {
    // Get raw data from touch sensor hardware
    *raw_data = touch_pad_get_raw_data(channel);
}

fsm_config_t config = DEFAULTS_TOUCH_SENSOR_FSM_CONFIG();
config.mode = FSM_MODE_POLLING;
config.polling_cb = polling_callback;
config.polling_interval = 20; // 20ms polling interval
```

### User Push Mode (`FSM_MODE_USER_PUSH`)
In this mode, the application is responsible for pushing new touch sensor data to the FSM.

Example:
```c
fsm_config_t config = DEFAULTS_TOUCH_SENSOR_FSM_CONFIG();
config.mode = FSM_MODE_USER_PUSH;

// Later in your code:
uint32_t raw_data = touch_pad_get_raw_data(channel);
touch_sensor_fsm_update_data(handle, channel, raw_data, false);
```

## API Usage Examples

### Creating and Starting the FSM

```c
// Initialize configuration
fsm_config_t config = DEFAULTS_TOUCH_SENSOR_FSM_CONFIG();
config.channel_num = 1;
config.channel_list = (uint32_t[]){0};
config.threshold_p = (float[]){0.1};

// Create FSM
fsm_handle_t handle;
ESP_ERROR_CHECK(touch_sensor_fsm_create(&config, &handle));

// Start FSM
ESP_ERROR_CHECK(touch_sensor_fsm_control(handle, FSM_CTRL_START, NULL));
```

### Handling Events

```c
void touch_task(void *arg) {
    fsm_handle_t handle = (fsm_handle_t)arg;
    while (1) {
        esp_err_t ret = touch_sensor_fsm_handle_events(handle);
        if (ret != ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

### Getting Data and State

```c
// Get processed data
uint32_t data[FSM_DATA_MAX];
ESP_ERROR_CHECK(touch_sensor_fsm_get_data(handle, channel, data));
printf("Raw: %lu, Smooth: %lu, Baseline: %lu\n",
       data[FSM_DATA_RAW],
       data[FSM_DATA_SMOOTH],
       data[FSM_DATA_BASELINE]);

// Get current state
fsm_state_t state;
ESP_ERROR_CHECK(touch_sensor_fsm_get_state(handle, channel, &state));
printf("State: %d\n", state);
```

### Cleanup

```c
ESP_ERROR_CHECK(touch_sensor_fsm_control(handle, FSM_CTRL_STOP, NULL));
ESP_ERROR_CHECK(touch_sensor_fsm_delete(handle));
```

