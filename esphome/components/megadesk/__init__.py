import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

megadesk_ns = cg.esphome_ns.namespace("megadesk")
MegadeskComponent = megadesk_ns.class_("Megadesk", cg.Component, uart.UARTDevice)

CONF_RAW_HEIGHT = "raw_height"
CONF_MIN_HEIGHT = "min_height"
CONF_MAX_HEIGHT = "max_height"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MegadeskComponent),
            cv.Optional(CONF_RAW_HEIGHT): sensor.sensor_schema(
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_MIN_HEIGHT): sensor.sensor_schema(
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_MAX_HEIGHT): sensor.sensor_schema(
                accuracy_decimals=0,
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "megadesk",
    baud_rate=115200,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if raw_config := config.get(CONF_RAW_HEIGHT):
        sens = await sensor.new_sensor(raw_config)
        cg.add(var.set_raw_height_sensor(sens))

    if min_config := config.get(CONF_MIN_HEIGHT):
        sens = await sensor.new_sensor(min_config)
        cg.add(var.set_min_height_sensor(sens))

    if max_config := config.get(CONF_MAX_HEIGHT):
        sens = await sensor.new_sensor(max_config)
        cg.add(var.set_max_height_sensor(sens))
