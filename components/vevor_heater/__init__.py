import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart, text_sensor, binary_sensor, number, switch, climate
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
    CONF_NAME,
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
    ICON_THERMOMETER,
    ICON_FLASH,
    ICON_FAN,
    ICON_POWER,
)

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "climate"]
DEPENDENCIES = ["uart"]

vevor_heater_ns = cg.esphome_ns.namespace("vevor_heater")
VevorHeater = vevor_heater_ns.class_("VevorHeater", cg.PollingComponent)
VevorClimate = vevor_heater_ns.class_("VevorClimate", climate.Climate, cg.Component)

# Configuration keys
CONF_AUTO_SENSORS = "auto_sensors"
CONF_CLIMATE_MODE = "climate_mode"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_CURRENT_TEMPERATURE = "current_temperature"
CONF_MIN_TEMPERATURE = "min_temperature"
CONF_MAX_TEMPERATURE = "max_temperature"
CONF_CONTROL_MODE = "control_mode"
CONF_DEFAULT_POWER_PERCENT = "default_power_percent"
CONF_EXTERNAL_TEMPERATURE_SENSOR = "external_temperature_sensor"

# Control mode options
CONTROL_MODE_MANUAL = "manual"
CONTROL_MODE_AUTOMATIC = "automatic"

# Sensor configuration keys
CONF_TEMPERATURE = "temperature"
CONF_INPUT_VOLTAGE = "input_voltage"
CONF_STATE = "state"
CONF_POWER_LEVEL = "power_level"
CONF_FAN_SPEED = "fan_speed"
CONF_PUMP_FREQUENCY = "pump_frequency"
CONF_GLOW_PLUG_CURRENT = "glow_plug_current"
CONF_HEAT_EXCHANGER_TEMPERATURE = "heat_exchanger_temperature"
CONF_STATE_DURATION = "state_duration"
CONF_COOLING_DOWN = "cooling_down"

# Simplified sensor schemas with good defaults
SENSOR_SCHEMAS = {
    CONF_TEMPERATURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
        icon=ICON_THERMOMETER,
    ),
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
}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(VevorHeater),
            cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_AUTO_SENSORS, default=True): cv.boolean,
            cv.Optional(CONF_CLIMATE_MODE, default=False): cv.boolean,
            cv.Optional(CONF_CONTROL_MODE, default=CONTROL_MODE_MANUAL): cv.enum(
                {CONTROL_MODE_MANUAL: "manual", CONTROL_MODE_AUTOMATIC: "automatic"},
                upper=False
            ),
            cv.Optional(CONF_DEFAULT_POWER_PERCENT, default=80.0): cv.float_range(
                min=10.0, max=100.0
            ),
            cv.Optional(CONF_EXTERNAL_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_TARGET_TEMPERATURE, default=20.0): cv.float_range(
                min=5.0, max=35.0
            ),
            cv.Optional(CONF_MIN_TEMPERATURE, default=5.0): cv.float_range(
                min=0.0, max=30.0
            ),
            cv.Optional(CONF_MAX_TEMPERATURE, default=35.0): cv.float_range(
                min=10.0, max=50.0
            ),
            # Individual sensor overrides (optional)
            cv.Optional(CONF_TEMPERATURE): SENSOR_SCHEMAS[CONF_TEMPERATURE],
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
    else:
        cg.add(var.set_control_mode(cg.RawExpression("esphome::vevor_heater::ControlMode::MANUAL")))
    
    # Set default power percent
    cg.add(var.set_default_power_percent(config[CONF_DEFAULT_POWER_PERCENT]))
    
    # Set external temperature sensor if provided
    if CONF_EXTERNAL_TEMPERATURE_SENSOR in config:
        external_sensor = await cg.get_variable(config[CONF_EXTERNAL_TEMPERATURE_SENSOR])
        cg.add(var.set_external_temperature_sensor(external_sensor))

    # Auto-create sensors if enabled
    if config[CONF_AUTO_SENSORS]:
        sensors_to_create = [
            (CONF_TEMPERATURE, "set_temperature_sensor"),
            (CONF_INPUT_VOLTAGE, "set_input_voltage_sensor"),
            (CONF_POWER_LEVEL, "set_power_level_sensor"),
            (CONF_FAN_SPEED, "set_fan_speed_sensor"),
            (CONF_PUMP_FREQUENCY, "set_pump_frequency_sensor"),
            (CONF_GLOW_PLUG_CURRENT, "set_glow_plug_current_sensor"),
            (CONF_HEAT_EXCHANGER_TEMPERATURE, "set_heat_exchanger_temperature_sensor"),
            (CONF_STATE_DURATION, "set_state_duration_sensor"),
        ]

        text_sensors_to_create = [
            (CONF_STATE, "set_state_sensor"),
        ]

        binary_sensors_to_create = [
            (CONF_COOLING_DOWN, "set_cooling_down_sensor"),
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
            (CONF_TEMPERATURE, "set_temperature_sensor", sensor.new_sensor),
            (CONF_INPUT_VOLTAGE, "set_input_voltage_sensor", sensor.new_sensor),
            (CONF_POWER_LEVEL, "set_power_level_sensor", sensor.new_sensor),
            (CONF_FAN_SPEED, "set_fan_speed_sensor", sensor.new_sensor),
            (CONF_PUMP_FREQUENCY, "set_pump_frequency_sensor", sensor.new_sensor),
            (CONF_GLOW_PLUG_CURRENT, "set_glow_plug_current_sensor", sensor.new_sensor),
            (CONF_HEAT_EXCHANGER_TEMPERATURE, "set_heat_exchanger_temperature_sensor", sensor.new_sensor),
            (CONF_STATE_DURATION, "set_state_duration_sensor", sensor.new_sensor),
            (CONF_STATE, "set_state_sensor", text_sensor.new_text_sensor),
            (CONF_COOLING_DOWN, "set_cooling_down_sensor", binary_sensor.new_binary_sensor),
        ]

        for sensor_key, setter_method, new_sensor_func in sensor_configs:
            if sensor_key in config:
                sens = await new_sensor_func(config[sensor_key])
                cg.add(getattr(var, setter_method)(sens))

    # Climate mode setup
    if config[CONF_CLIMATE_MODE]:
        climate_var = cg.new_Pvariable(config[CONF_ID] + "_climate")
        await cg.register_component(climate_var, config)
        cg.add(climate_var.set_vevor_heater(var))
        cg.add(climate_var.set_target_temperature(config[CONF_TARGET_TEMPERATURE]))
        cg.add(climate_var.set_min_temperature(config[CONF_MIN_TEMPERATURE]))
        cg.add(climate_var.set_max_temperature(config[CONF_MAX_TEMPERATURE]))
