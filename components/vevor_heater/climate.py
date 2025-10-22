import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID
from . import vevor_heater_ns, VevorHeater, CONF_VEVOR_HEATER_ID

AUTO_LOAD = ["vevor_heater"]
DEPENDENCIES = ["climate"]

VevorClimate = vevor_heater_ns.class_("VevorClimate", climate.Climate, cg.Component)

CONF_VEVOR_HEATER_ID = "vevor_heater_id"
CONF_MIN_TEMPERATURE = "min_temperature"
CONF_MAX_TEMPERATURE = "max_temperature"

CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(VevorClimate),
        cv.Required(CONF_VEVOR_HEATER_ID): cv.use_id(VevorHeater),
        cv.Optional(CONF_MIN_TEMPERATURE, default=5.0): cv.float_range(min=0, max=30),
        cv.Optional(CONF_MAX_TEMPERATURE, default=35.0): cv.float_range(min=10, max=50),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    
    heater = await cg.get_variable(config[CONF_VEVOR_HEATER_ID])
    cg.add(var.set_vevor_heater(heater))
    
    cg.add(var.set_min_temperature(config[CONF_MIN_TEMPERATURE]))
    cg.add(var.set_max_temperature(config[CONF_MAX_TEMPERATURE]))
