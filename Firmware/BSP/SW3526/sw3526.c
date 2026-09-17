#include "sw3526.h"
#include <string.h>

static SW3526_IOResult map_result(SoftI2C_Result result)
{
    switch(result)
    {
        case SOFT_I2C_RESULT_OK:      return SW3526_IO_OK;
        case SOFT_I2C_RESULT_NACK:    return SW3526_IO_NACK;
        case SOFT_I2C_RESULT_TIMEOUT:   return SW3526_IO_TIMEOUT;
        case SOFT_I2C_RESULT_BUS_STUCK: return SW3526_IO_BUS_STUCK;
        default:                        return SW3526_IO_ERROR;
    }
}

static void note_result(SW3526_Handle *handle, SoftI2C_Result result)
{
    handle->last_io = map_result(result);
    handle->status.online = (result == SOFT_I2C_RESULT_OK) ? 1u : 0u;

    /* Recovery is deliberately restricted to fault paths; normal runtime I/O
     * remains cooperative and non-blocking. */
    if(result == SOFT_I2C_RESULT_TIMEOUT || result == SOFT_I2C_RESULT_BUS_STUCK)
        (void)SoftI2C_Recover(handle->i2c);
}

#define SW3526_SPAWN_IO(call_expr)                                      \
    do {                                                                 \
        CORO_INIT(&handle->io_pt);                                       \
        CORO_WAIT_UNTIL(pt, ((call_expr) == CORO_ENDED));                \
        if(handle->last_io != SW3526_IO_OK)                              \
        {                                                                \
            handle->status.online = 0u;                                  \
            SW3526_RETURN_END();                                         \
        }                                                                \
    } while(0)

static uint16_t decode_adc12(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 4) | ((uint16_t)bytes[1] & 0x0Fu));
}

static uint8_t encode_power_w(uint8_t watts, uint8_t *code)
{
    if(code == 0)
        return 0u;

    if(watts >= 12u && watts <= 63u)
    {
        *code = watts;
        return 1u;
    }

    if(watts >= 64u && watts <= 71u)
    {
        *code = (uint8_t)(watts - 64u);
        return 1u;
    }

    return 0u;
}


static void decode_protocol_config(SW3526_Handle *handle)
{
    SW3526_ProtocolMask mask = 0u;
    uint8_t fc0;
    uint8_t fc1;
    uint8_t fc2;
    uint8_t fc4;

    if(handle == 0)
        return;

    fc0 = handle->protocol_config.raw_fc0;
    fc1 = handle->protocol_config.raw_fc1;
    fc2 = handle->protocol_config.raw_fc2;
    fc4 = handle->protocol_config.raw_fc4;

    /* Reg0xAC[2] is a global gate.  Reg0xAA[5] separately gates the
     * DPDM/BC block. */
    if((fc4 & SW3526_FC4_DISABLE_FAST_CHARGE) == 0u)
    {
        if((fc2 & SW3526_FC2_DISABLE_DPDM) == 0u)
        {
            if((fc0 & SW3526_FC0_DISABLE_QC2) == 0u)  mask |= SW3526_PROTOCOL_ENABLE_QC2;
            if((fc0 & SW3526_FC0_DISABLE_QC3) == 0u)  mask |= SW3526_PROTOCOL_ENABLE_QC3;
            if((fc0 & SW3526_FC0_DISABLE_FCP) == 0u)  mask |= SW3526_PROTOCOL_ENABLE_FCP;
            if((fc0 & SW3526_FC0_DISABLE_SCP) == 0u)  mask |= SW3526_PROTOCOL_ENABLE_SCP;
            if((fc0 & SW3526_FC0_DISABLE_PE) == 0u)   mask |= SW3526_PROTOCOL_ENABLE_PE;
            if((fc0 & SW3526_FC0_DISABLE_VOOC) == 0u) mask |= SW3526_PROTOCOL_ENABLE_VOOC;
            if((fc0 & SW3526_FC0_DISABLE_SFCP) == 0u) mask |= SW3526_PROTOCOL_ENABLE_SFCP;
            if((fc0 & SW3526_FC0_DISABLE_AFC) == 0u)  mask |= SW3526_PROTOCOL_ENABLE_AFC;
        }

        if((fc1 & SW3526_FC1_DISABLE_PD) == 0u)
            mask |= SW3526_PROTOCOL_ENABLE_PD;
    }

    handle->protocol_config.enabled_mask = mask;
}

static void decode_protection_status(SW3526_Handle *handle)
{
    uint8_t fault;
    uint8_t fc3;

    if(handle == 0)
        return;

    fault = handle->protection.raw_fault;
    fc3 = handle->protection.raw_fc3;

    handle->protection.vin_ovp =
        (fault & SW3526_STRG_FAULT_VIN_OVP) ? 1u : 0u;
    handle->protection.over_temperature_alarm =
        (fault & SW3526_STRG_FAULT_OTP_ALARM) ? 1u : 0u;
    handle->protection.over_temperature_shutdown =
        (fault & SW3526_STRG_FAULT_OTP_SHUTDOWN) ? 1u : 0u;
    handle->protection.output_short =
        (fault & SW3526_STRG_FAULT_SHORT) ? 1u : 0u;
    handle->protection.any_fault =
        (fault & (SW3526_STRG_FAULT_VIN_OVP |
                  SW3526_STRG_FAULT_OTP_ALARM |
                  SW3526_STRG_FAULT_OTP_SHUTDOWN |
                  SW3526_STRG_FAULT_SHORT)) ? 1u : 0u;

    handle->protection.pd_overcurrent_oc_mode =
        (fc3 & SW3526_FC3_PD_OCP_OC_MODE) ? 1u : 0u;
    handle->protection.qc3_overcurrent_voltage_drop_mode =
        (fc3 & SW3526_FC3_QC3_OCP_VDROP_MODE) ? 1u : 0u;
    handle->protection.qc3_constant_power =
        (fc3 & SW3526_FC3_QC3_CONST_POWER) ? 1u : 0u;
    handle->protection.pps_constant_power =
        (fc3 & SW3526_FC3_PPS_CONST_POWER) ? 1u : 0u;
}

void SW3526_HandleInit(SW3526_Handle *handle, SoftI2C_Handle *i2c)
{
    if(handle == 0)
        return;
    memset(handle, 0, sizeof(*handle));
    handle->i2c = i2c;
    handle->address = SW3526_I2C_ADDR;
    handle->last_io = SW3526_IO_IDLE;
    CORO_INIT(&handle->io_pt);
}

SW3526_RET SW3526_ByteWrite(SW3526_ARGS(uint8_t reg, uint8_t data))
{
    SoftI2C_Result result;
    SW3526_FUNC_BEGIN;

    if(handle == 0 || handle->i2c == 0)
        SW3526_RETURN_END();

    handle->txbuf[0] = reg;
    handle->txbuf[1] = data;
    SW3526_UNTIL(SoftI2C_TryWrite(handle->i2c, handle->address, handle->txbuf, 2u));
    SW3526_UNTIL(SoftI2C_GetResult(handle->i2c) != SOFT_I2C_RESULT_ACTIVE);
    result = SoftI2C_TakeResult(handle->i2c);
    note_result(handle, result);

    SW3526_FUNC_END;
}

SW3526_RET SW3526_ByteRead(SW3526_ARGS(uint8_t reg, uint8_t *data))
{
    SoftI2C_Result result;
    SW3526_FUNC_BEGIN;

    if(handle == 0 || handle->i2c == 0 || data == 0)
        SW3526_RETURN_END();

    handle->txbuf[0] = reg;
    SW3526_UNTIL(SoftI2C_TryWriteRead(handle->i2c, handle->address,
                                      handle->txbuf, 1u, data, 1u));
    SW3526_UNTIL(SoftI2C_GetResult(handle->i2c) != SOFT_I2C_RESULT_ACTIVE);
    result = SoftI2C_TakeResult(handle->i2c);
    note_result(handle, result);

    SW3526_FUNC_END;
}

SW3526_RET SW3526_BytesRead(SW3526_ARGS(uint8_t reg, uint8_t *data, uint16_t len))
{
    SoftI2C_Result result;
    SW3526_FUNC_BEGIN;

    if(handle == 0 || handle->i2c == 0 || data == 0 || len == 0u)
        SW3526_RETURN_END();

    handle->txbuf[0] = reg;
    SW3526_UNTIL(SoftI2C_TryWriteRead(handle->i2c, handle->address,
                                      handle->txbuf, 1u, data, len));
    SW3526_UNTIL(SoftI2C_GetResult(handle->i2c) != SOFT_I2C_RESULT_ACTIVE);
    result = SoftI2C_TakeResult(handle->i2c);
    note_result(handle, result);

    SW3526_FUNC_END;
}

SW3526_RET SW3526_UnlockWrite(SW3526_NOARG)
{
    SW3526_FUNC_BEGIN;
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP1));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP2));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP3));
    SW3526_FUNC_END;
}

SW3526_RET SW3526_ADCRead(SW3526_ARGS(uint8_t channel, uint16_t *raw))
{
    SW3526_FUNC_BEGIN;
    if(raw == 0)
        SW3526_RETURN_END();

    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_ADC_SET, channel));
    SW3526_SPAWN_IO(SW3526_BytesRead(&handle->io_pt, handle, SW3526_STRG_ADCH, handle->rxbuf, 2u));
    *raw = decode_adc12(handle->rxbuf);
    SW3526_FUNC_END;
}

SW3526_RET SW3526_ADCLoad(SW3526_NOARG)
{
    SW3526_FUNC_BEGIN;

    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_ADC_SET, SW3526_ADC_SET_VIN));
    SW3526_SPAWN_IO(SW3526_BytesRead(&handle->io_pt, handle, SW3526_STRG_ADCH, handle->rxbuf, 2u));
    handle->status.vin_raw = decode_adc12(handle->rxbuf);
    handle->status.vin_mv = (uint16_t)(handle->status.vin_raw * 10u);

    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_ADC_SET, SW3526_ADC_SET_VOUT));
    SW3526_SPAWN_IO(SW3526_BytesRead(&handle->io_pt, handle, SW3526_STRG_ADCH, handle->rxbuf, 2u));
    handle->status.vout_raw = decode_adc12(handle->rxbuf);
    handle->status.vout_mv = (uint16_t)(handle->status.vout_raw * 6u);

    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_ADC_SET, SW3526_ADC_SET_IOUT));
    SW3526_SPAWN_IO(SW3526_BytesRead(&handle->io_pt, handle, SW3526_STRG_ADCH, handle->rxbuf, 2u));
    handle->status.iout_raw = decode_adc12(handle->rxbuf);
    handle->status.iout_ma_x10 = (uint32_t)handle->status.iout_raw * 25u;

    SW3526_FUNC_END;
}

SW3526_RET SW3526_ProtocolLoad(SW3526_NOARG)
{
    SW3526_FUNC_BEGIN;
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_PROTOCOL, &handle->status.protocol));
    SW3526_FUNC_END;
}

SW3526_RET SW3526_PortStatusLoad(SW3526_NOARG)
{
    SW3526_FUNC_BEGIN;
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_SYS_STAT, &handle->status.sys_stat));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_FAULT, &handle->status.fault));
    handle->protection.raw_fault = handle->status.fault;
    decode_protection_status(handle);
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_POWER, &handle->status.power_state));
    handle->status.power_state &= 0x7Fu;
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_CC_STATE, &handle->status.cc_state));
    SW3526_FUNC_END;
}

SW3526_RET SW3526_StatusLoad(SW3526_NOARG)
{
    SW3526_FUNC_BEGIN;

    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_REV, &handle->status.version));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_PROTOCOL, &handle->status.protocol));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_SYS_STAT, &handle->status.sys_stat));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_FAULT, &handle->status.fault));
    handle->protection.raw_fault = handle->status.fault;
    decode_protection_status(handle);
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_POWER, &handle->status.power_state));
    handle->status.power_state &= 0x7Fu;
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_STRG_CC_STATE, &handle->status.cc_state));

    SW3526_FUNC_END;
}


SW3526_RET SW3526_ProtocolConfigLoad(SW3526_NOARG)
{
    SW3526_FUNC_BEGIN;

    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG0,
                                     &handle->protocol_config.raw_fc0));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG1,
                                     &handle->protocol_config.raw_fc1));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG2,
                                     &handle->protocol_config.raw_fc2));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG4,
                                     &handle->protocol_config.raw_fc4));
    decode_protocol_config(handle);

    SW3526_FUNC_END;
}

SW3526_RET SW3526_SetProtocolMask(SW3526_ARGS(SW3526_ProtocolMask mask))
{
    SW3526_FUNC_BEGIN;

    if(handle == 0 || (mask & (SW3526_ProtocolMask)~SW3526_PROTOCOL_ENABLE_ALL) != 0u)
    {
        if(handle != 0)
            handle->last_io = SW3526_IO_ERROR;
        SW3526_RETURN_END();
    }

    /* Read-modify-write is intentional: RG012_1 marks reserved/config fields
     * as values that must not be changed. */
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG0,
                                     &handle->protocol_config.raw_fc0));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG1,
                                     &handle->protocol_config.raw_fc1));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG2,
                                     &handle->protocol_config.raw_fc2));
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG4,
                                     &handle->protocol_config.raw_fc4));

    /* Config0 uses active-low disable bits. */
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_SCP)  ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_SCP)  : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_SCP));
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_VOOC) ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_VOOC) : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_VOOC));
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_SFCP) ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_SFCP) : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_SFCP));
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_QC2)  ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_QC2)  : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_QC2));
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_QC3)  ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_QC3)  : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_QC3));
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_FCP)  ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_FCP)  : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_FCP));
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_AFC)  ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_AFC)  : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_AFC));
    handle->protocol_config.raw_fc0 =
        (uint8_t)((mask & SW3526_PROTOCOL_ENABLE_PE)   ? (handle->protocol_config.raw_fc0 & (uint8_t)~SW3526_FC0_DISABLE_PE)   : (handle->protocol_config.raw_fc0 | SW3526_FC0_DISABLE_PE));

    /* Master PD enable only.  Individual 9/12/15/20 V and PPS slots are
     * preserved exactly as they were. */
    if(mask & SW3526_PROTOCOL_ENABLE_PD)
        handle->protocol_config.raw_fc1 &= (uint8_t)~SW3526_FC1_DISABLE_PD;
    else
        handle->protocol_config.raw_fc1 |= SW3526_FC1_DISABLE_PD;

    /* DPDM block is only required by the non-PD protocol families above. */
    if(mask & SW3526_PROTOCOL_ENABLE_DPDM_ALL)
        handle->protocol_config.raw_fc2 &= (uint8_t)~SW3526_FC2_DISABLE_DPDM;
    else
        handle->protocol_config.raw_fc2 |= SW3526_FC2_DISABLE_DPDM;

    /* Keep the chip-level fast-charge gate coherent with the requested mask. */
    if(mask != 0u)
        handle->protocol_config.raw_fc4 &= (uint8_t)~SW3526_FC4_DISABLE_FAST_CHARGE;
    else
        handle->protocol_config.raw_fc4 |= SW3526_FC4_DISABLE_FAST_CHARGE;

    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP1));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP2));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP3));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle,
                                      SW3526_CTRG_FC_CONFIG0,
                                      handle->protocol_config.raw_fc0));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle,
                                      SW3526_CTRG_FC_CONFIG1,
                                      handle->protocol_config.raw_fc1));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle,
                                      SW3526_CTRG_FC_CONFIG2,
                                      handle->protocol_config.raw_fc2));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle,
                                      SW3526_CTRG_FC_CONFIG4,
                                      handle->protocol_config.raw_fc4));

    decode_protocol_config(handle);
    SW3526_FUNC_END;
}

SW3526_RET SW3526_ProtectionStatusLoad(SW3526_NOARG)
{
    SW3526_FUNC_BEGIN;

    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_STRG_FAULT,
                                     &handle->protection.raw_fault));
    handle->status.fault = handle->protection.raw_fault;
    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle,
                                     SW3526_CTRG_FC_CONFIG3,
                                     &handle->protection.raw_fc3));
    decode_protection_status(handle);

    SW3526_FUNC_END;
}

SW3526_RET SW3526_SetPowerLimitW(SW3526_ARGS(uint8_t watts))
{
    SW3526_FUNC_BEGIN;

    if(handle == 0 || !encode_power_w(watts, &handle->op_u8))
    {
        if(handle != 0)
            handle->last_io = SW3526_IO_ERROR;
        SW3526_RETURN_END();
    }

    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP1));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP2));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_WREN, SW3526_WREN_STEP3));

    SW3526_SPAWN_IO(SW3526_ByteRead(&handle->io_pt, handle, SW3526_CTRG_FC_CONFIG3, &handle->rxbuf[0]));
    handle->rxbuf[0] |= SW3526_CTRG_FC_CONFIG3_REG_PWR;
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_FC_CONFIG3, handle->rxbuf[0]));
    SW3526_SPAWN_IO(SW3526_ByteWrite(&handle->io_pt, handle, SW3526_CTRG_POWER_CONFIG, handle->op_u8));

    handle->status.configured_power_w = watts;
    SW3526_FUNC_END;
}

const struct SW3526_StatusTypedef *SW3526_GetStatus(const SW3526_Handle *handle)
{
    return (handle != 0) ? &handle->status : 0;
}

const SW3526_ProtocolConfig *SW3526_GetProtocolConfig(const SW3526_Handle *handle)
{
    return (handle != 0) ? &handle->protocol_config : 0;
}

const SW3526_ProtectionStatus *SW3526_GetProtectionStatus(const SW3526_Handle *handle)
{
    return (handle != 0) ? &handle->protection : 0;
}

uint8_t SW3526_IsOnline(const SW3526_Handle *handle)
{
    return (handle != 0) ? handle->status.online : 0u;
}

uint8_t SW3526_GetPortStatus(const SW3526_Handle *handle,
                             SW3526_PortStatus *status)
{
    if(handle == 0 || status == 0)
        return 0u;

    status->online = handle->status.online;
    status->protocol_online =
        (handle->status.protocol & SW3526_STRG_PROTOCOL_ONLINE) ? 1u : 0u;
    status->high_voltage =
        (handle->status.protocol & SW3526_STRG_PROTOCOL_HIGH_VOLTAGE) ? 1u : 0u;
    status->port_on =
        (handle->status.sys_stat & SW3526_STRG_SYS_STAT_PORT_ON) ? 1u : 0u;
    status->buck_on =
        (handle->status.sys_stat & SW3526_STRG_SYS_STAT_BUCK_ON) ? 1u : 0u;
    status->fault = handle->status.fault;
    status->power_w = handle->status.power_state;
    return handle->status.online;
}

uint8_t SW3526_IsProtocolOnline(const SW3526_Handle *handle)
{
    return (handle != 0 && (handle->status.protocol & SW3526_STRG_PROTOCOL_ONLINE) != 0u) ? 1u : 0u;
}

uint8_t SW3526_IsPortOn(const SW3526_Handle *handle)
{
    return (handle != 0 && (handle->status.sys_stat & SW3526_STRG_SYS_STAT_PORT_ON) != 0u) ? 1u : 0u;
}

uint8_t SW3526_IsBuckOn(const SW3526_Handle *handle)
{
    return (handle != 0 && (handle->status.sys_stat & SW3526_STRG_SYS_STAT_BUCK_ON) != 0u) ? 1u : 0u;
}

uint8_t SW3526_IsCC1Attached(const SW3526_Handle *handle)
{
    return (handle != 0 && (handle->status.cc_state & SW3526_STRG_CC_STATE_CC1) != 0u) ? 1u : 0u;
}

uint8_t SW3526_IsCC2Attached(const SW3526_Handle *handle)
{
    return (handle != 0 && (handle->status.cc_state & SW3526_STRG_CC_STATE_CC2) != 0u) ? 1u : 0u;
}

uint8_t SW3526_GetPDVersion(const SW3526_Handle *handle)
{
    if(handle == 0)
        return 0u;
    return (uint8_t)((handle->status.protocol & SW3526_STRG_PROTOCOL_PD_VER_MSK) >> SW3526_STRG_PROTOCOL_PD_VER_SHIFT);
}

SW3526_Protocol SW3526_GetProtocol(const SW3526_Handle *handle)
{
    if(handle == 0)
        return SW3526_PROTOCOL_NONE;
    return (SW3526_Protocol)(handle->status.protocol & SW3526_STRG_PROTOCOL_TYPE_MSK);
}

uint16_t SW3526_ReadVINmV(const SW3526_Handle *handle)
{
    return (handle != 0) ? handle->status.vin_mv : 0u;
}

uint16_t SW3526_ReadVOUTmV(const SW3526_Handle *handle)
{
    return (handle != 0) ? handle->status.vout_mv : 0u;
}

uint32_t SW3526_ReadIOUTmA_x10(const SW3526_Handle *handle)
{
    return (handle != 0) ? handle->status.iout_ma_x10 : 0u;
}
