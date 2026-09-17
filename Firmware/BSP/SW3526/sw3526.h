/* SW3526 operation library
 * TKWTL Desktop PD Power Converter
 * Register map basis: iSW RG012_1 v1.0
 */
#ifndef __SW3526_H__
#define __SW3526_H__

#include <stdint.h>
#include "coroOS.h"
#include "soft_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SW3526_I2C_ADDR                 0x3CU

/* Register naming follows the SW6306 library convention:
 *   STRG = status/readback register
 *   CTRG = control/configuration register
 */
#define SW3526_STRG_REV                 0x01U
#define SW3526_STRG_BUCK_VOUTH          0x03U
#define SW3526_STRG_BUCK_VOUTL          0x04U
#define SW3526_STRG_BUCK_ILIM           0x05U
#define SW3526_STRG_PROTOCOL            0x06U
#define SW3526_STRG_SYS_STAT            0x07U
#define SW3526_STRG_FAULT               0x0BU
#define SW3526_CTRG_WREN                0x12U
#define SW3526_CTRG_FORCE_OFF           0x13U
#define SW3526_STRG_ADC_VIN             0x30U
#define SW3526_STRG_ADC_VOUT            0x31U
#define SW3526_STRG_ADC_IOUT            0x33U
#define SW3526_CTRG_ADC_SET             0x3AU
#define SW3526_STRG_ADCH                0x3BU
#define SW3526_STRG_ADCL                0x3CU
#define SW3526_STRG_POWER               0x68U
#define SW3526_STRG_CC_STATE            0x69U
#define SW3526_CTRG_POWER_CONFIG        0xA7U
#define SW3526_CTRG_FC_CONFIG0          0xA8U
#define SW3526_CTRG_FC_CONFIG1          0xA9U
#define SW3526_CTRG_FC_CONFIG2          0xAAU
#define SW3526_CTRG_FC_CONFIG3          0xABU
#define SW3526_CTRG_FC_CONFIG4          0xACU
#define SW3526_CTRG_FC_CONFIG3_REG_PWR  0x04U

/* 0x06 protocol indication. */
#define SW3526_STRG_PROTOCOL_ONLINE          0x80U
#define SW3526_STRG_PROTOCOL_HIGH_VOLTAGE    0x40U
#define SW3526_STRG_PROTOCOL_PD_VER_MSK      0x30U
#define SW3526_STRG_PROTOCOL_PD_VER_SHIFT    4U
#define SW3526_STRG_PROTOCOL_TYPE_MSK        0x0FU

/* 0x07 system status. */
#define SW3526_STRG_SYS_STAT_PORT_ON         0x02U
#define SW3526_STRG_SYS_STAT_BUCK_ON         0x01U

/* 0x0B abnormal case. */
#define SW3526_STRG_FAULT_VIN_OVP            0x10U
#define SW3526_STRG_FAULT_OTP_ALARM          0x04U
#define SW3526_STRG_FAULT_OTP_SHUTDOWN       0x02U
#define SW3526_STRG_FAULT_SHORT              0x01U

/* 0x69 Type-C CC attachment status. */
#define SW3526_STRG_CC_STATE_CC1             0x80U
#define SW3526_STRG_CC_STATE_CC2             0x40U

/* 0xA8 Fast Charge Config0.  These bits are active-low: 0 = enabled. */
#define SW3526_FC0_DISABLE_SCP               0x80U
#define SW3526_FC0_DISABLE_VOOC              0x40U
#define SW3526_FC0_DISABLE_SFCP              0x20U
#define SW3526_FC0_DISABLE_QC2               0x10U
#define SW3526_FC0_DISABLE_QC3               0x08U
#define SW3526_FC0_DISABLE_FCP               0x04U
#define SW3526_FC0_DISABLE_AFC               0x02U
#define SW3526_FC0_DISABLE_PE                0x01U

/* 0xA9 Fast Charge Config1.  Master PD enable is active-low.
 * PDO/PPS slot bits are deliberately left untouched by SW3526_SetProtocolMask(). */
#define SW3526_FC1_DISABLE_PPS1              0x80U
#define SW3526_FC1_DISABLE_PPS0              0x40U
#define SW3526_FC1_DISABLE_PD20              0x20U
#define SW3526_FC1_DISABLE_PD15              0x10U
#define SW3526_FC1_DISABLE_PD12              0x08U
#define SW3526_FC1_DISABLE_PD9               0x04U
#define SW3526_FC1_DISABLE_PD                0x01U

/* 0xAA Fast Charge Config2: bit5 is also active-low. */
#define SW3526_FC2_DISABLE_DPDM              0x20U

/* 0xAB protection/power behaviour. */
#define SW3526_FC3_PD_OCP_OC_MODE            0x80U
#define SW3526_FC3_QC3_OCP_VDROP_MODE        0x40U
#define SW3526_FC3_QC3_CONST_POWER           0x20U
#define SW3526_FC3_PPS_CONST_POWER           0x10U

/* 0xAC global fast-charge gate. */
#define SW3526_FC4_DISABLE_FAST_CHARGE       0x04U

/* 0x3A ADC data type. Full 12-bit data appears at 0x3B/0x3C. */
#define SW3526_ADC_SET_VIN              0x01U  /* 10 mV/bit */
#define SW3526_ADC_SET_VOUT             0x02U  /* 6 mV/bit */
#define SW3526_ADC_SET_IOUT             0x03U  /* 2.5 mA/bit */

#define SW3526_WREN_STEP1               0x20U
#define SW3526_WREN_STEP2               0x40U
#define SW3526_WREN_STEP3               0x80U

typedef enum
{
    SW3526_PROTOCOL_NONE = 0,
    SW3526_PROTOCOL_QC2 = 1,
    SW3526_PROTOCOL_QC3 = 2,
    SW3526_PROTOCOL_FCP = 3,
    SW3526_PROTOCOL_SCP = 4,
    SW3526_PROTOCOL_PD_FIXED = 5,
    SW3526_PROTOCOL_PD_PPS = 6,
    SW3526_PROTOCOL_PE11 = 7,
    SW3526_PROTOCOL_PE20 = 8,
    SW3526_PROTOCOL_VOOC = 9,
    SW3526_PROTOCOL_SFCP = 10,
    SW3526_PROTOCOL_AFC = 11
} SW3526_Protocol;


/* High-level protocol-family mask.  PD is treated as one family: this API
 * changes Reg0xA9[0] only and intentionally preserves the board's existing
 * fixed-PDO/PPS slot selection in Reg0xA9[7:2]. */
typedef uint16_t SW3526_ProtocolMask;

#define SW3526_PROTOCOL_ENABLE_QC2      ((SW3526_ProtocolMask)1u << 0)
#define SW3526_PROTOCOL_ENABLE_QC3      ((SW3526_ProtocolMask)1u << 1)
#define SW3526_PROTOCOL_ENABLE_FCP      ((SW3526_ProtocolMask)1u << 2)
#define SW3526_PROTOCOL_ENABLE_SCP      ((SW3526_ProtocolMask)1u << 3)
#define SW3526_PROTOCOL_ENABLE_PD       ((SW3526_ProtocolMask)1u << 4)
#define SW3526_PROTOCOL_ENABLE_PE       ((SW3526_ProtocolMask)1u << 5)
#define SW3526_PROTOCOL_ENABLE_VOOC     ((SW3526_ProtocolMask)1u << 6)
#define SW3526_PROTOCOL_ENABLE_SFCP     ((SW3526_ProtocolMask)1u << 7)
#define SW3526_PROTOCOL_ENABLE_AFC      ((SW3526_ProtocolMask)1u << 8)
#define SW3526_PROTOCOL_ENABLE_ALL      ((SW3526_ProtocolMask)0x01FFu)
#define SW3526_PROTOCOL_ENABLE_DPDM_ALL ((SW3526_ProtocolMask)( \
    SW3526_PROTOCOL_ENABLE_QC2  | SW3526_PROTOCOL_ENABLE_QC3  | \
    SW3526_PROTOCOL_ENABLE_FCP  | SW3526_PROTOCOL_ENABLE_SCP  | \
    SW3526_PROTOCOL_ENABLE_PE   | SW3526_PROTOCOL_ENABLE_VOOC | \
    SW3526_PROTOCOL_ENABLE_SFCP | SW3526_PROTOCOL_ENABLE_AFC))

typedef struct
{
    SW3526_ProtocolMask enabled_mask;
    uint8_t raw_fc0;
    uint8_t raw_fc1;
    uint8_t raw_fc2;
    uint8_t raw_fc4;
} SW3526_ProtocolConfig;

typedef struct
{
    uint8_t raw_fault;
    uint8_t raw_fc3;
    uint8_t any_fault;
    uint8_t vin_ovp;
    uint8_t over_temperature_alarm;
    uint8_t over_temperature_shutdown;
    uint8_t output_short;
    uint8_t pd_overcurrent_oc_mode;
    uint8_t qc3_overcurrent_voltage_drop_mode;
    uint8_t qc3_constant_power;
    uint8_t pps_constant_power;
} SW3526_ProtectionStatus;

typedef enum
{
    SW3526_IO_IDLE = 0,
    SW3526_IO_OK,
    SW3526_IO_NACK,
    SW3526_IO_TIMEOUT,
    SW3526_IO_BUS_STUCK,
    SW3526_IO_ERROR
} SW3526_IOResult;

struct SW3526_StatusTypedef
{
    uint8_t online;
    uint8_t version;
    uint8_t protocol;
    uint8_t sys_stat;
    uint8_t fault;
    uint8_t power_state;
    uint8_t cc_state;
    uint8_t configured_power_w;

    uint16_t vin_raw;
    uint16_t vout_raw;
    uint16_t iout_raw;
    uint16_t vin_mv;
    uint16_t vout_mv;
    uint32_t iout_ma_x10; /* 0.1 mA units; 2.5 mA/bit -> raw*25 */
};

typedef struct
{
    uint8_t online;
    uint8_t protocol_online;
    uint8_t high_voltage;
    uint8_t port_on;
    uint8_t buck_on;
    uint8_t fault;
    uint8_t power_w;
} SW3526_PortStatus;

typedef struct
{
    SoftI2C_Handle *i2c;
    uint8_t address;
    volatile SW3526_IOResult last_io;
    struct SW3526_StatusTypedef status;
    SW3526_ProtocolConfig protocol_config;
    SW3526_ProtectionStatus protection;

    /* Per-handle persistent coroutine/I2C storage. No global transfer state. */
    coro_pt_t io_pt;
    uint8_t txbuf[4];
    uint8_t rxbuf[4];

    /* Scratch that must survive coroutine yields. */
    uint8_t op_u8;
} SW3526_Handle;

/* SW6306-style coroutine syntax adapted to coroOS and explicit handles. */
#define SW3526_RET              uint8_t
#define SW3526_ARGS(...)        coro_pt_t *pt, SW3526_Handle *handle, __VA_ARGS__
#define SW3526_NOARG            coro_pt_t *pt, SW3526_Handle *handle
#define SW3526_FUNC_BEGIN       THRD_BEGIN
#define SW3526_FUNC_END         THRD_END
#define SW3526_UNTIL(cond)      THRD_UNTIL(cond)
#define SW3526_RETURN_END()     do { CORO_INIT(pt); return CORO_ENDED; } while(0)

void SW3526_HandleInit(SW3526_Handle *handle, SoftI2C_Handle *i2c);

SW3526_RET SW3526_ByteWrite(SW3526_ARGS(uint8_t reg, uint8_t data));
SW3526_RET SW3526_ByteRead(SW3526_ARGS(uint8_t reg, uint8_t *data));
SW3526_RET SW3526_BytesRead(SW3526_ARGS(uint8_t reg, uint8_t *data, uint16_t len));
SW3526_RET SW3526_UnlockWrite(SW3526_NOARG);
SW3526_RET SW3526_ADCRead(SW3526_ARGS(uint8_t channel, uint16_t *raw));
SW3526_RET SW3526_ADCLoad(SW3526_NOARG);
SW3526_RET SW3526_ProtocolLoad(SW3526_NOARG);
SW3526_RET SW3526_PortStatusLoad(SW3526_NOARG);
SW3526_RET SW3526_StatusLoad(SW3526_NOARG);

/* Protocol configuration is intentionally family-level.  It preserves
 * Reg0xA9[7:2] (individual PD fixed/PPS profile slots) and only toggles the
 * master PD bit plus the documented DPDM family enables. */
SW3526_RET SW3526_ProtocolConfigLoad(SW3526_NOARG);
SW3526_RET SW3526_SetProtocolMask(SW3526_ARGS(SW3526_ProtocolMask mask));

/* Reads the live Reg0x0B fault flags and Reg0xAB protection modes. */
SW3526_RET SW3526_ProtectionStatusLoad(SW3526_NOARG);

/* Valid range: 12..71 W.  The API switches SW3526 to register-based power
 * setting (Reg0xAB[2]=1) and writes Reg0xA7. */
SW3526_RET SW3526_SetPowerLimitW(SW3526_ARGS(uint8_t watts));

const struct SW3526_StatusTypedef *SW3526_GetStatus(const SW3526_Handle *handle);
const SW3526_ProtocolConfig *SW3526_GetProtocolConfig(const SW3526_Handle *handle);
const SW3526_ProtectionStatus *SW3526_GetProtectionStatus(const SW3526_Handle *handle);
uint8_t SW3526_GetPortStatus(const SW3526_Handle *handle,
                             SW3526_PortStatus *status);
uint8_t SW3526_IsOnline(const SW3526_Handle *handle);
uint8_t SW3526_IsProtocolOnline(const SW3526_Handle *handle);
uint8_t SW3526_IsPortOn(const SW3526_Handle *handle);
uint8_t SW3526_IsBuckOn(const SW3526_Handle *handle);
uint8_t SW3526_IsCC1Attached(const SW3526_Handle *handle);
uint8_t SW3526_IsCC2Attached(const SW3526_Handle *handle);
uint8_t SW3526_GetPDVersion(const SW3526_Handle *handle);
SW3526_Protocol SW3526_GetProtocol(const SW3526_Handle *handle);
uint16_t SW3526_ReadVINmV(const SW3526_Handle *handle);
uint16_t SW3526_ReadVOUTmV(const SW3526_Handle *handle);
uint32_t SW3526_ReadIOUTmA_x10(const SW3526_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* __SW3526_H__ */
