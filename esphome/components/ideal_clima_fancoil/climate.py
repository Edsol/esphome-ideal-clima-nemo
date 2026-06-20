import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, number, switch, uart
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_MODE,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
    UNIT_CELSIUS,
)

CODEOWNERS = ["@community"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["climate", "number", "switch"]

CONF_COOL_MIN_TEMPERATURE = "cool_min_temperature"
CONF_COOL_MAX_TEMPERATURE = "cool_max_temperature"
CONF_HEAT_MIN_TEMPERATURE = "heat_min_temperature"
CONF_HEAT_MAX_TEMPERATURE = "heat_max_temperature"
CONF_DRY_MIN_TEMPERATURE = "dry_min_temperature"
CONF_DRY_MAX_TEMPERATURE = "dry_max_temperature"
CONF_FAN_MIN_TEMPERATURE = "fan_min_temperature"
CONF_FAN_MAX_TEMPERATURE = "fan_max_temperature"
CONF_INITIAL_VALUE = "initial_value"
CONF_MODEL_POWER = "model_power"
CONF_DEBUG_LOGGING = "debug_logging"

ideal_clima_ns = cg.esphome_ns.namespace("ideal_clima_fancoil")
IdealClimaFancoil = ideal_clima_ns.class_(
    "IdealClimaFancoil", climate.Climate, cg.Component, uart.UARTDevice
)
IdealClimaLimitNumber = ideal_clima_ns.class_(
    "IdealClimaLimitNumber", number.Number
)
IdealClimaDebugSwitch = ideal_clima_ns.class_(
    "IdealClimaDebugSwitch", switch.Switch
)

LIMIT_COOL_MIN = 0
LIMIT_COOL_MAX = 1
LIMIT_HEAT_MIN = 2
LIMIT_HEAT_MAX = 3
LIMIT_DRY_MIN = 4
LIMIT_DRY_MAX = 5
LIMIT_FAN_MIN = 6
LIMIT_FAN_MAX = 7


def limit_number_schema(name, initial_value):
    return number.number_schema(
        IdealClimaLimitNumber,
        device_class=DEVICE_CLASS_TEMPERATURE,
        unit_of_measurement=UNIT_CELSIUS,
        entity_category=ENTITY_CATEGORY_CONFIG,
    ).extend(
        {
            cv.Optional("name", default=name): cv.string,
            cv.Optional(CONF_MODE, default="BOX"): cv.enum(number.NUMBER_MODES, upper=True),
            cv.Optional(CONF_ENTITY_CATEGORY, default=ENTITY_CATEGORY_CONFIG): cv.entity_category,
            cv.Optional(CONF_INITIAL_VALUE, default=initial_value): cv.float_range(min=0, max=40),
        }
    )

CONFIG_SCHEMA = (
    climate.climate_schema(IdealClimaFancoil)
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(
        {
            cv.Optional(CONF_MODEL_POWER, default="1000"): cv.string,
            cv.Optional(
                CONF_DEBUG_LOGGING,
                default={"name": "Protocol debug logging"},
            ): switch.switch_schema(
                IdealClimaDebugSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="RESTORE_DEFAULT_OFF",
            ),
            cv.Optional(
                CONF_COOL_MIN_TEMPERATURE,
                default={"name": "Cool min temperature", CONF_INITIAL_VALUE: 8},
            ): limit_number_schema("Cool min temperature", 8),
            cv.Optional(
                CONF_COOL_MAX_TEMPERATURE,
                default={"name": "Cool max temperature", CONF_INITIAL_VALUE: 40},
            ): limit_number_schema("Cool max temperature", 40),
            cv.Optional(
                CONF_HEAT_MIN_TEMPERATURE,
                default={"name": "Heat min temperature", CONF_INITIAL_VALUE: 0},
            ): limit_number_schema("Heat min temperature", 0),
            cv.Optional(
                CONF_HEAT_MAX_TEMPERATURE,
                default={"name": "Heat max temperature", CONF_INITIAL_VALUE: 40},
            ): limit_number_schema("Heat max temperature", 40),
            cv.Optional(
                CONF_DRY_MIN_TEMPERATURE,
                default={"name": "Dry min temperature", CONF_INITIAL_VALUE: 0},
            ): limit_number_schema("Dry min temperature", 0),
            cv.Optional(
                CONF_DRY_MAX_TEMPERATURE,
                default={"name": "Dry max temperature", CONF_INITIAL_VALUE: 40},
            ): limit_number_schema("Dry max temperature", 40),
            cv.Optional(
                CONF_FAN_MIN_TEMPERATURE,
                default={"name": "Fan min temperature", CONF_INITIAL_VALUE: 0},
            ): limit_number_schema("Fan min temperature", 0),
            cv.Optional(
                CONF_FAN_MAX_TEMPERATURE,
                default={"name": "Fan max temperature", CONF_INITIAL_VALUE: 40},
            ): limit_number_schema("Fan max temperature", 40),
        }
    )
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_model_power(config[CONF_MODEL_POWER]))

    debug_switch = await switch.new_switch(config[CONF_DEBUG_LOGGING], var)
    cg.add(var.set_debug_switch(debug_switch))

    for key, limit_id in [
        (CONF_COOL_MIN_TEMPERATURE, LIMIT_COOL_MIN),
        (CONF_COOL_MAX_TEMPERATURE, LIMIT_COOL_MAX),
        (CONF_HEAT_MIN_TEMPERATURE, LIMIT_HEAT_MIN),
        (CONF_HEAT_MAX_TEMPERATURE, LIMIT_HEAT_MAX),
        (CONF_DRY_MIN_TEMPERATURE, LIMIT_DRY_MIN),
        (CONF_DRY_MAX_TEMPERATURE, LIMIT_DRY_MAX),
        (CONF_FAN_MIN_TEMPERATURE, LIMIT_FAN_MIN),
        (CONF_FAN_MAX_TEMPERATURE, LIMIT_FAN_MAX),
    ]:
        limit = await number.new_number(
            config[key],
            var,
            limit_id,
            min_value=0,
            max_value=40,
            step=1,
        )
        cg.add(var.set_limit_number(limit_id, limit))
        cg.add(var.set_limit_default(limit_id, config[key][CONF_INITIAL_VALUE]))
