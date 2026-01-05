import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart, text_sensor, binary_sensor, number, switch, button, time, select
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
    CONF_NAME,
    CONF_TIME_ID,
    UNIT_CELSIUS,
    UNIT_VOLT,
    UNIT_AMPERE,
    UNIT_HERTZ,
    UNIT_REVOLUTIONS_PER_MINUTE,
    UNIT_SECOND,
    UNIT_PERCENT,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    ICON_THERMOMETER,
    ICON_FLASH,
    ICON_FAN,
    ICON_POWER,
)

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "number", "button", "select", "switch"]
DEPENDENCIES = ["uart"]

vevor_heater_ns = cg.esphome_ns.namespace("vevor_heater")
VevorHeater = vevor_heater_ns.class_("VevorHeater", cg.PollingComponent)
VevorInjectedPerPulseNumber = vevor_heater_ns.class_("VevorInjectedPerPulseNumber", number.Number, cg.Component)
VevorResetTotalConsumptionButton = vevor_heater_ns.class_("VevorResetTotalConsumptionButton", button.Button, cg.Component)
VevorControlModeSelect = vevor_heater_ns.class_("VevorControlModeSelect", select.Select, cg.Component)
VevorHeaterPowerSwitch = vevor_heater_ns.class_("VevorHeaterPowerSwitch", switch.Switch, cg.Component)
VevorHeaterPowerLevelNumber = vevor_heater_ns.class_("VevorHeaterPowerLevelNumber", number.Number, cg.Component)

# Configuration keys
CONF_AUTO_SENSORS = "auto_sensors"
CONF_CURRENT_TEMPERATURE = "current_temperature"
CONF_CONTROL_MODE = "control_mode"
CONF_CONTROL_MODE_SELECT = "control_mode_select"
CONF_DEFAULT_POWER_PERCENT = "default_power_percent"
CONF_EXTERNAL_TEMPERATURE_SENSOR = "external_temperature_sensor"
CONF_INJECTED_PER_PULSE = "injected_per_pulse"
CONF_INJECTED_PER_PULSE_NUMBER = "injected_per_pulse_number"
CONF_POLLING_INTERVAL = "polling_interval"
CONF_RESET_TOTAL_CONSUMPTION_BUTTON = "reset_total_consumption_button"
CONF_POWER_SWITCH = "power_switch"
CONF_POWER_LEVEL_NUMBER = "power_level_number"

# Control mode options
CONTROL_MODE_MANUAL = "manual"
CONTROL_MODE_AUTOMATIC = "automatic"
CONTROL_MODE_ANTIFREEZE = "antifreeze"

# Sensor configuration keys - removed CONF_TEMPERATURE (duplicate)
CONF_INPUT_VOLTAGE = "input_voltage"
CONF_STATE = "state"
CONF_POWER_LEVEL = "power_level"
CONF_FAN_SPEED = "fan_speed"
CONF_PUMP_FREQUENCY = "pump_frequency"
CONF_GLOW_PLUG_CURRENT = "glow_plug_current"
CONF_HEAT_EXCHANGER_TEMPERATURE = "heat_exchanger_temperature"
CONF_STATE_DURATION = "state_duration"
CONF_COOLING_DOWN = "cooling_down"
CONF_HOURLY_CONSUMPTION = "hourly_consumption"
CONF_DAILY_CONSUMPTION = "daily_consumption"
CONF_TOTAL_CONSUMPTION = "total_consumption"
CONF_LOW_VOLTAGE_ERROR = "low_voltage_error"

# Fuel consumption constants
UNIT_MILLILITERS = "ml"
UNIT_MILLILITERS_PER_HOUR = "ml/h"

# Simplified sensor schemas with good defaults - removed duplicate temperature sensor
SENSOR_SCHEMAS = {
    CONF_INPUT_VOLTAGE: sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
        icon=ICON_FLASH,
    ),
    CONF_STATE: text_sensor.text_sensor_schema(
        icon=ICON_POWER,
    ),
    CONF_POWER_LEVEL: sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
        icon=ICON_POWER,
    ),
    CONF_FAN_SPEED: sensor.sensor_schema(
        unit_of_measurement=UNIT_REVOLUTIONS_PER_MINUTE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
        icon=ICON_FAN,
    ),
    CONF_PUMP_FREQUENCY: sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
    ),
    CONF_GLOW_PLUG_CURRENT: sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
    ),
    CONF_HEAT_EXCHANGER_TEMPERATURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
        icon=ICON_THERMOMETER,
    ),
    CONF_STATE_DURATION: sensor.sensor_schema(
        unit_of_measurement=UNIT_SECOND,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    CONF_COOLING_DOWN: binary_sensor.binary_sensor_schema(
        icon=ICON_FAN,
    ),
    CONF_LOW_VOLTAGE_ERROR: binary_sensor.binary_sensor_schema(
        icon="mdi:alert-circle",
        device_class="problem",
    ),
    CONF_HOURLY_CONSUMPTION: sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLILITERS_PER_HOUR,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=2,
        icon="mdi:fuel",
    ),
    CONF_DAILY_CONSUMPTION: sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLILITERS,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        accuracy_decimals=2,
        icon="mdi:counter",
    ),    CONF_TOTAL_CONSUMPTION: sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLILITERS,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        accuracy_decimals=2,
        icon="mdi:fuel",
    ),}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(VevorHeater),
            cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_AUTO_SENSORS, default=True): cv.boolean,
            cv.Optional(CONF_CONTROL_MODE, default=CONTROL_MODE_MANUAL): cv.enum(
                {CONTROL_MODE_MANUAL: "manual", CONTROL_MODE_AUTOMATIC: "automatic", CONTROL_MODE_ANTIFREEZE: "antifreeze"},
                upper=False
            ),
            cv.Optional(CONF_DEFAULT_POWER_PERCENT, default=80.0): cv.float_range(
                min=10.0, max=100.0
            ),
            cv.Optional(CONF_INJECTED_PER_PULSE, default=0.022): cv.float_range(
                min=0.001, max=1.0
            ),
            cv.Optional(CONF_POLLING_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_EXTERNAL_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional("min_voltage_start", default=12.3): cv.float_range(
                min=10.0, max=15.0
            ),
            cv.Optional("min_voltage_operate", default=11.4): cv.float_range(
                min=9.0, max=14.0
            ),
            # Antifreeze mode temperature thresholds
            cv.Optional("antifreeze_temp_on", default=2.0): cv.float_range(
                min=-20.0, max=20.0
            ),
            cv.Optional("antifreeze_temp_medium", default=6.0): cv.float_range(
                min=-20.0, max=20.0
            ),
            cv.Optional("antifreeze_temp_low", default=8.0): cv.float_range(
                min=-20.0, max=20.0
            ),
            cv.Optional("antifreeze_temp_off", default=9.0): cv.float_range(
                min=-20.0, max=30.0
            ),
            # Individual sensor overrides (optional) - removed duplicate temperature sensor
            cv.Optional(CONF_INPUT_VOLTAGE): SENSOR_SCHEMAS[CONF_INPUT_VOLTAGE],
            cv.Optional(CONF_STATE): SENSOR_SCHEMAS[CONF_STATE],
            cv.Optional(CONF_POWER_LEVEL): SENSOR_SCHEMAS[CONF_POWER_LEVEL],
            cv.Optional(CONF_FAN_SPEED): SENSOR_SCHEMAS[CONF_FAN_SPEED],
            cv.Optional(CONF_PUMP_FREQUENCY): SENSOR_SCHEMAS[CONF_PUMP_FREQUENCY],
            cv.Optional(CONF_GLOW_PLUG_CURRENT): SENSOR_SCHEMAS[CONF_GLOW_PLUG_CURRENT],
            cv.Optional(CONF_HEAT_EXCHANGER_TEMPERATURE): SENSOR_SCHEMAS[
                CONF_HEAT_EXCHANGER_TEMPERATURE
            ],
            cv.Optional(CONF_STATE_DURATION): SENSOR_SCHEMAS[CONF_STATE_DURATION],
            cv.Optional(CONF_COOLING_DOWN): SENSOR_SCHEMAS[CONF_COOLING_DOWN],
            cv.Optional(CONF_LOW_VOLTAGE_ERROR): SENSOR_SCHEMAS[CONF_LOW_VOLTAGE_ERROR],
            cv.Optional(CONF_HOURLY_CONSUMPTION): SENSOR_SCHEMAS[CONF_HOURLY_CONSUMPTION],
            cv.Optional(CONF_DAILY_CONSUMPTION): SENSOR_SCHEMAS[CONF_DAILY_CONSUMPTION],
            cv.Optional(CONF_TOTAL_CONSUMPTION): SENSOR_SCHEMAS[CONF_TOTAL_CONSUMPTION],
            # Number component for injected per pulse
            cv.Optional(CONF_INJECTED_PER_PULSE_NUMBER): number.number_schema(
                VevorInjectedPerPulseNumber,
                unit_of_measurement=UNIT_MILLILITERS,
                icon="mdi:eyedropper",
                entity_category="config",
            ).extend({
                cv.Optional("min_value", default=0.001): cv.float_,
                cv.Optional("max_value", default=1.0): cv.float_,
                cv.Optional("step", default=0.001): cv.float_,
            }),
            # Button to reset total consumption
            cv.Optional(CONF_RESET_TOTAL_CONSUMPTION_BUTTON): button.button_schema(
                VevorResetTotalConsumptionButton,
                icon="mdi:restart",
                entity_category="config",
            ),
            # Select for control mode
            cv.Optional(CONF_CONTROL_MODE_SELECT): select.select_schema(
                VevorControlModeSelect,
                icon="mdi:format-list-bulleted",
                entity_category="config",
            ),
            # Switch for heater power control
            cv.Optional(CONF_POWER_SWITCH): switch.switch_schema(
                VevorHeaterPowerSwitch,
                icon="mdi:fire",
            ),
            # Number for power level control
            cv.Optional(CONF_POWER_LEVEL_NUMBER): number.number_schema(
                VevorHeaterPowerLevelNumber,
                unit_of_measurement=UNIT_PERCENT,
                icon="mdi:percent",
            ).extend({
                cv.Optional("min_value", default=10.0): cv.float_,
                cv.Optional("max_value", default=100.0): cv.float_,
                cv.Optional("step", default=10.0): cv.float_,
            }),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(cv.polling_component_schema("1s")),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    uart_component = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart_parent(uart_component))

    # Set control mode
    control_mode = config[CONF_CONTROL_MODE]
    if control_mode == CONTROL_MODE_AUTOMATIC:
        cg.add(var.set_control_mode(cg.RawExpression("esphome::vevor_heater::ControlMode::AUTOMATIC")))
    elif control_mode == CONTROL_MODE_ANTIFREEZE:
        cg.add(var.set_control_mode(cg.RawExpression("esphome::vevor_heater::ControlMode::ANTIFREEZE")))
    else:
        cg.add(var.set_control_mode(cg.RawExpression("esphome::vevor_heater::ControlMode::MANUAL")))
    
    # Set default power percent
    cg.add(var.set_default_power_percent(config[CONF_DEFAULT_POWER_PERCENT]))
    
    # Set injected per pulse
    cg.add(var.set_injected_per_pulse(config[CONF_INJECTED_PER_PULSE]))
    
    # Set polling interval
    cg.add(var.set_polling_interval(config[CONF_POLLING_INTERVAL]))
    
    # Set voltage safety thresholds
    cg.add(var.set_min_voltage_start(config["min_voltage_start"]))
    cg.add(var.set_min_voltage_operate(config["min_voltage_operate"]))
    
    # Set antifreeze temperature thresholds
    cg.add(var.set_antifreeze_temp_on(config["antifreeze_temp_on"]))
    cg.add(var.set_antifreeze_temp_medium(config["antifreeze_temp_medium"]))
    cg.add(var.set_antifreeze_temp_low(config["antifreeze_temp_low"]))
    cg.add(var.set_antifreeze_temp_off(config["antifreeze_temp_off"]))
    
    # Set time component if provided
    if CONF_TIME_ID in config:
        time_component = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time_component(time_component))
    
    # Set external temperature sensor if provided
    if CONF_EXTERNAL_TEMPERATURE_SENSOR in config:
        external_sensor = await cg.get_variable(config[CONF_EXTERNAL_TEMPERATURE_SENSOR])
        cg.add(var.set_external_temperature_sensor(external_sensor))

    # Auto-create sensors if enabled
    if config[CONF_AUTO_SENSORS]:
        sensors_to_create = [
            # Removed duplicate (CONF_TEMPERATURE, "set_temperature_sensor"),
            (CONF_INPUT_VOLTAGE, "set_input_voltage_sensor"),
            (CONF_POWER_LEVEL, "set_power_level_sensor"),
            (CONF_FAN_SPEED, "set_fan_speed_sensor"),
            (CONF_PUMP_FREQUENCY, "set_pump_frequency_sensor"),
            (CONF_GLOW_PLUG_CURRENT, "set_glow_plug_current_sensor"),
            (CONF_HEAT_EXCHANGER_TEMPERATURE, "set_heat_exchanger_temperature_sensor"),
            (CONF_STATE_DURATION, "set_state_duration_sensor"),
            (CONF_HOURLY_CONSUMPTION, "set_hourly_consumption_sensor"),
            (CONF_DAILY_CONSUMPTION, "set_daily_consumption_sensor"),
            (CONF_TOTAL_CONSUMPTION, "set_total_consumption_sensor"),
        ]

        text_sensors_to_create = [
            (CONF_STATE, "set_state_sensor"),
        ]

        binary_sensors_to_create = [
            (CONF_COOLING_DOWN, "set_cooling_down_sensor"),
            (CONF_LOW_VOLTAGE_ERROR, "set_low_voltage_error_sensor"),
        ]

        # Create regular sensors
        for sensor_key, setter_method in sensors_to_create:
            if sensor_key in config:
                # Use user-provided configuration
                sens_config = config[sensor_key]
            else:
                # Use default configuration with automatic naming and ID
                sens_config = {
                    CONF_ID: cg.RawExpression(f"{config[CONF_ID]}_sensor_{sensor_key}"),
                    CONF_NAME: f"Vevor Heater {sensor_key.replace('_', ' ').title()}"
                }
                # Apply the schema to get proper defaults
                sens_config = SENSOR_SCHEMAS[sensor_key](sens_config)
            
            sens = await sensor.new_sensor(sens_config)
            cg.add(getattr(var, setter_method)(sens))

        # Create text sensors
        for sensor_key, setter_method in text_sensors_to_create:
            if sensor_key in config:
                sens_config = config[sensor_key]
            else:
                sens_config = {
                    CONF_ID: cg.RawExpression(f"{config[CONF_ID]}_text_sensor_{sensor_key}"),
                    CONF_NAME: f"Vevor Heater {sensor_key.replace('_', ' ').title()}"
                }
                sens_config = SENSOR_SCHEMAS[sensor_key](sens_config)
            
            sens = await text_sensor.new_text_sensor(sens_config)
            cg.add(getattr(var, setter_method)(sens))

        # Create binary sensors
        for sensor_key, setter_method in binary_sensors_to_create:
            if sensor_key in config:
                sens_config = config[sensor_key]
            else:
                sens_config = {
                    CONF_ID: cg.RawExpression(f"{config[CONF_ID]}_binary_sensor_{sensor_key}"),
                    CONF_NAME: f"Vevor Heater {sensor_key.replace('_', ' ').title()}"
                }
                sens_config = SENSOR_SCHEMAS[sensor_key](sens_config)
            
            sens = await binary_sensor.new_binary_sensor(sens_config)
            cg.add(getattr(var, setter_method)(sens))
    else:
        # Manual sensor configuration
        sensor_configs = [
            # Removed duplicate (CONF_TEMPERATURE, "set_temperature_sensor", sensor.new_sensor),
            (CONF_INPUT_VOLTAGE, "set_input_voltage_sensor", sensor.new_sensor),
            (CONF_POWER_LEVEL, "set_power_level_sensor", sensor.new_sensor),
            (CONF_FAN_SPEED, "set_fan_speed_sensor", sensor.new_sensor),
            (CONF_PUMP_FREQUENCY, "set_pump_frequency_sensor", sensor.new_sensor),
            (CONF_GLOW_PLUG_CURRENT, "set_glow_plug_current_sensor", sensor.new_sensor),
            (CONF_HEAT_EXCHANGER_TEMPERATURE, "set_heat_exchanger_temperature_sensor", sensor.new_sensor),
            (CONF_STATE_DURATION, "set_state_duration_sensor", sensor.new_sensor),
            (CONF_HOURLY_CONSUMPTION, "set_hourly_consumption_sensor", sensor.new_sensor),
            (CONF_DAILY_CONSUMPTION, "set_daily_consumption_sensor", sensor.new_sensor),
            (CONF_TOTAL_CONSUMPTION, "set_total_consumption_sensor", sensor.new_sensor),
            (CONF_STATE, "set_state_sensor", text_sensor.new_text_sensor),
            (CONF_COOLING_DOWN, "set_cooling_down_sensor", binary_sensor.new_binary_sensor),
            (CONF_LOW_VOLTAGE_ERROR, "set_low_voltage_error_sensor", binary_sensor.new_binary_sensor),
        ]

        for sensor_key, setter_method, new_sensor_func in sensor_configs:
            if sensor_key in config:
                sens = await new_sensor_func(config[sensor_key])
                cg.add(getattr(var, setter_method)(sens))

    # Number component for injected per pulse
    if CONF_INJECTED_PER_PULSE_NUMBER in config:
        num_config = config[CONF_INJECTED_PER_PULSE_NUMBER]
        num = await number.new_number(num_config, min_value=num_config["min_value"], max_value=num_config["max_value"], step=num_config["step"])
        cg.add(num.set_vevor_heater(var))
        cg.add(var.set_injected_per_pulse_number(num))
    
    # Button component for resetting total consumption
    if CONF_RESET_TOTAL_CONSUMPTION_BUTTON in config:
        btn = await button.new_button(config[CONF_RESET_TOTAL_CONSUMPTION_BUTTON])
        cg.add(btn.set_vevor_heater(var))
    
    # Select component for control mode
    if CONF_CONTROL_MODE_SELECT in config:
        sel = await select.new_select(config[CONF_CONTROL_MODE_SELECT], options=["Manual", "Antifreeze"])  # "Automatic" commented out
        cg.add(sel.set_vevor_heater(var))
    
    # Switch component for heater power
    if CONF_POWER_SWITCH in config:
        sw = await switch.new_switch(config[CONF_POWER_SWITCH])
        cg.add(sw.set_vevor_heater(var))
    
    # Number component for power level
    if CONF_POWER_LEVEL_NUMBER in config:
        num_config = config[CONF_POWER_LEVEL_NUMBER]
        num = await number.new_number(num_config, min_value=num_config["min_value"], max_value=num_config["max_value"], step=num_config["step"])
        cg.add(num.set_vevor_heater(var))