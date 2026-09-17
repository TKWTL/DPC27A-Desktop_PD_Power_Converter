#include "sw3538.h"
#include <string.h>

static SW3538_IOResult map_result(I2C_API_Result result)
{
    switch(result)
    {
        case I2C_API_RESULT_OK:      return SW3538_IO_OK;
        case I2C_API_RESULT_NACK:    return SW3538_IO_NACK;
        case I2C_API_RESULT_TIMEOUT: return SW3538_IO_TIMEOUT;
        default:                     return SW3538_IO_ERROR;
    }
}

#define SW3538_SPAWN_IO(call_expr)                                      \
    do {                                                                 \
        CORO_INIT(&handle->io_pt);                                       \
        CORO_WAIT_UNTIL(pt, ((call_expr) == CORO_ENDED));                \
        if(handle->last_io != SW3538_IO_OK)                              \
        {                                                                \
            handle->status.online = 0u;                                  \
            SW3538_RETURN_END();                                         \
        }                                                                \
    } while(0)


/* Same leaf-I/O wait as SW3538_SPAWN_IO, but remember the first error instead
 * of returning immediately.  Extended-bank operations use this form so that
 * Reg0x180 can always be written to restore the normal register bank. */
#define SW3538_CAPTURE_IO(call_expr)                                    \
    do {                                                                 \
        CORO_INIT(&handle->io_pt);                                       \
        CORO_WAIT_UNTIL(pt, ((call_expr) == CORO_ENDED));                \
        if((handle->last_io != SW3538_IO_OK) &&                          \
           (handle->op_result == SW3538_IO_OK))                          \
            handle->op_result = handle->last_io;                         \
    } while(0)

static uint16_t decode_adc12(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (((uint16_t)bytes[1] & 0x0Fu) << 8));
}

static uint16_t decode_ilim_ma(uint8_t raw)
{
    if(raw & 0x80u)
        return (uint16_t)(500u + 25u * (uint16_t)(raw & 0x7Fu));

    return (uint16_t)(1000u + 50u * (uint16_t)(raw & 0x7Fu));
}

static uint8_t adc_force_mask(uint8_t channel)
{
    switch(channel)
    {
        case SW3538_ADC_SET_PT1_IOUT: return SW3538_CTRG_FORCE2_ADC_PT1_I;
        case SW3538_ADC_SET_PT2_IOUT: return SW3538_CTRG_FORCE2_ADC_PT2_I;
        case SW3538_ADC_SET_VOUT:
        case SW3538_ADC_SET_VOUT_MV:  return SW3538_CTRG_FORCE2_ADC_VOUT;
        case SW3538_ADC_SET_VIN:      return SW3538_CTRG_FORCE2_ADC_VIN;
        case SW3538_ADC_SET_VNTC:     return SW3538_CTRG_FORCE2_ADC_NTC;
        default:                       return 0u;
    }
}


static void decode_protocol_config(SW3538_Handle *handle)
{
    SW3538_ProtocolMask mask = 0u;

    if(handle == 0)
        return;

    if(handle->protocol_config.raw_dpdm_cfg0 & SW3538_XREG_DPDM_CFG0_BC_ENABLE)
    {
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_QC2_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_QC2;
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_QC3_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_QC3;
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_QC3P_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_QC3P;
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_QC4P_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_QC4P;
        if(handle->protocol_config.raw_dpdm_cfg3 & SW3538_XREG_DPDM_CFG3_FCP_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_FCP;
        if(handle->protocol_config.raw_dpdm_cfg3 &
           (SW3538_XREG_DPDM_CFG3_SCP_LV_ENABLE | SW3538_XREG_DPDM_CFG3_SCP_HV_ENABLE))
            mask |= SW3538_PROTOCOL_ENABLE_SCP;
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_PE_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_PE;
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_SFCP_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_SFCP;
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_AFC_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_AFC;
        if(handle->protocol_config.raw_dpdm_cfg4 & SW3538_XREG_DPDM_CFG4_TFCP_ENABLE)
            mask |= SW3538_PROTOCOL_ENABLE_TFCP;
    }

    if(handle->protocol_config.raw_pd_cfg1 & SW3538_XREG_PD_CFG1_ENABLE)
        mask |= SW3538_PROTOCOL_ENABLE_PD;

    handle->protocol_config.enabled_mask = mask;
    handle->protocol_config.path1_fast_charge_enabled =
        (handle->protocol_config.raw_path_cfg &
         SW3538_XREG_PATH1_FAST_CHARGE_ENABLE) ? 1u : 0u;
    handle->protocol_config.path2_fast_charge_enabled =
        (handle->protocol_config.raw_path_cfg &
         SW3538_XREG_PATH2_FAST_CHARGE_ENABLE) ? 1u : 0u;
}

static void decode_protection_status(SW3538_Handle *handle)
{
    static const uint16_t uvlo_enter_mv[4] = {9000u, 4000u, 5000u, 9000u};
    static const uint16_t uvlo_exit_mv[4]  = {9750u, 4500u, 6000u, 9750u};
    static const uint16_t ovp_enter_mv[4]  = {30000u, 34500u, 37500u, 43500u};
    static const uint16_t ovp_exit_mv[4]   = {28500u, 33000u, 36000u, 42000u};
    uint8_t ntc_code;
    uint8_t uvlo_code;
    uint8_t ovp_code;

    if(handle == 0)
        return;

    handle->protection.fault_cause_available = 0u;

    handle->protection.buck_on =
        (handle->protection.raw_sys_stat0 & SW3538_STRG_SYS_STAT0_BUCK_ON) ? 1u : 0u;
    handle->protection.port1_on =
        (handle->protection.raw_sys_stat0 & SW3538_STRG_SYS_STAT0_PORT1_ON) ? 1u : 0u;
    handle->protection.port2_on =
        (handle->protection.raw_sys_stat0 & SW3538_STRG_SYS_STAT0_PORT2_ON) ? 1u : 0u;
    handle->protection.port1_device_online =
        (handle->protection.raw_sys_stat1 & SW3538_STRG_SYS_STAT1_PORT1_ONLINE) ? 1u : 0u;
    handle->protection.port2_device_online =
        (handle->protection.raw_sys_stat1 & SW3538_STRG_SYS_STAT1_PORT2_ONLINE) ? 1u : 0u;

    handle->protection.scene_change_event =
        (handle->protection.raw_irq & SW3538_STRG_IRQ_SCENE_CHANGE) ? 1u : 0u;
    handle->protection.buck_on_event =
        (handle->protection.raw_irq & SW3538_STRG_IRQ_BUCK_ON) ? 1u : 0u;
    handle->protection.buck_off_event =
        (handle->protection.raw_irq & SW3538_STRG_IRQ_BUCK_OFF) ? 1u : 0u;

    handle->protection.vin_ovp_enabled =
        (handle->protection.raw_sys_ctrl1 & SW3538_CTRG_SYS1_VIN_OVP_ENABLE) ? 1u : 0u;
    handle->protection.die_otp_enabled =
        (handle->protection.raw_sys_ctrl1 & SW3538_CTRG_SYS1_DIE_OTP_ENABLE) ? 1u : 0u;
    handle->protection.single_point_failure_enabled =
        (handle->protection.raw_single_fault_cfg &
         SW3538_XREG_SINGLE_FAULT_DISABLE) ? 0u : 1u;
    handle->protection.vout_uvp_enabled =
        (handle->protection.raw_single_fault_cfg &
         SW3538_XREG_VOUT_UVP_ENABLE) ? 1u : 0u;
    handle->protection.dpdm_ovp_enabled =
        (handle->protection.raw_vin_cfg & SW3538_XREG_DPDM_OVP_ENABLE) ? 1u : 0u;
    handle->protection.dpdm_weak_short_enabled =
        (handle->protection.raw_vin_cfg &
         SW3538_XREG_DPDM_WEAK_SHORT_ENABLE) ? 1u : 0u;
    handle->protection.cc_ovp_enabled =
        (handle->protection.raw_vin_cfg & SW3538_XREG_CC_OVP_ENABLE) ? 1u : 0u;

    ntc_code = (uint8_t)((handle->protection.raw_temp_cfg &
                          SW3538_XREG_TEMP_NTC_MSK) >>
                         SW3538_XREG_TEMP_NTC_SHIFT);
    if(ntc_code == SW3538_XREG_TEMP_NTC_DISABLED)
    {
        handle->protection.ntc_threshold_c = 0u;
        handle->protection.ntc_otp_enabled = 0u;
    }
    else
    {
        handle->protection.ntc_threshold_c =
            (uint8_t)(65u + (uint8_t)(ntc_code * 10u));
        handle->protection.ntc_otp_enabled =
            (handle->protection.raw_sys_ctrl1 &
             SW3538_CTRG_SYS1_NTC_OTP_ENABLE) ? 1u : 0u;
    }

    uvlo_code = (uint8_t)((handle->protection.raw_vin_cfg &
                           SW3538_XREG_VIN_UVLO_MSK) >>
                          SW3538_XREG_VIN_UVLO_SHIFT);
    ovp_code = (uint8_t)((handle->protection.raw_vin_cfg &
                          SW3538_XREG_VIN_OVP_MSK) >>
                         SW3538_XREG_VIN_OVP_SHIFT);

    handle->protection.vin_uvlo_enter_mv = uvlo_enter_mv[uvlo_code & 0x03u];
    handle->protection.vin_uvlo_exit_mv = uvlo_exit_mv[uvlo_code & 0x03u];
    handle->protection.vin_ovp_enter_mv = ovp_enter_mv[ovp_code & 0x03u];
    handle->protection.vin_ovp_exit_mv = ovp_exit_mv[ovp_code & 0x03u];
}

void SW3538_HandleInit(SW3538_Handle *handle, I2C_API_Owner owner)
{
    if(handle == 0)
        return;
    memset(handle, 0, sizeof(*handle));
    handle->address = SW3538_I2C_ADDR;
    handle->i2c_owner = owner;
    handle->last_io = SW3538_IO_IDLE;
    CORO_INIT(&handle->io_pt);
}

SW3538_RET SW3538_ByteWrite(SW3538_ARGS(uint8_t reg, uint8_t data))
{
    I2C_API_Result result;
    SW3538_FUNC_BEGIN;

    if(handle == 0 || handle->i2c_owner == I2C_API_OWNER_NONE)
        SW3538_RETURN_END();

    handle->txbuf[0] = reg;
    handle->txbuf[1] = data;
    SW3538_UNTIL(I2C_API_TryWrite(handle->i2c_owner, handle->address, handle->txbuf, 2u));
    SW3538_UNTIL(I2C_API_GetResult(handle->i2c_owner) != I2C_API_RESULT_ACTIVE);
    result = I2C_API_TakeResult(handle->i2c_owner);
    handle->last_io = map_result(result);
    handle->status.online = (result == I2C_API_RESULT_OK) ? 1u : 0u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_ByteRead(SW3538_ARGS(uint8_t reg, uint8_t *data))
{
    I2C_API_Result result;
    SW3538_FUNC_BEGIN;

    if(handle == 0 || data == 0 || handle->i2c_owner == I2C_API_OWNER_NONE)
        SW3538_RETURN_END();

    handle->txbuf[0] = reg;
    SW3538_UNTIL(I2C_API_TryWriteRead(handle->i2c_owner, handle->address,
                                      handle->txbuf, 1u, data, 1u));
    SW3538_UNTIL(I2C_API_GetResult(handle->i2c_owner) != I2C_API_RESULT_ACTIVE);
    result = I2C_API_TakeResult(handle->i2c_owner);
    handle->last_io = map_result(result);
    handle->status.online = (result == I2C_API_RESULT_OK) ? 1u : 0u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_BytesRead(SW3538_ARGS(uint8_t reg, uint8_t *data, uint16_t len))
{
    I2C_API_Result result;
    SW3538_FUNC_BEGIN;

    if(handle == 0 || data == 0 || len == 0u || handle->i2c_owner == I2C_API_OWNER_NONE)
        SW3538_RETURN_END();

    handle->txbuf[0] = reg;
    SW3538_UNTIL(I2C_API_TryWriteRead(handle->i2c_owner, handle->address,
                                      handle->txbuf, 1u, data, len));
    SW3538_UNTIL(I2C_API_GetResult(handle->i2c_owner) != I2C_API_RESULT_ACTIVE);
    result = I2C_API_TakeResult(handle->i2c_owner);
    handle->last_io = map_result(result);
    handle->status.online = (result == I2C_API_RESULT_OK) ? 1u : 0u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_UnlockWrite(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    SW3538_FUNC_END;
}

SW3538_RET SW3538_UnlockForce(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;
    /* Reg0x15 itself is an ordinary writable register, therefore perform the
     * general I2C write-enable sequence before the force-register sequence. */
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP1));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP2));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP3));
    SW3538_FUNC_END;
}

SW3538_RET SW3538_SetPowerLimitW(SW3538_ARGS(uint8_t watts))
{
    SW3538_FUNC_BEGIN;

    if(handle == 0 || watts < 18u || watts > 140u)
    {
        if(handle != 0)
            handle->last_io = SW3538_IO_ERROR;
        SW3538_RETURN_END();
    }

    /* P = 20 V * I, current code is 10 mA/bit -> watts * 5.  Keep it in
     * per-handle storage because this coroutine yields repeatedly. */
    handle->op_u16 = (uint16_t)watts * 5u;
    handle->op_result = SW3538_IO_OK;
    handle->op_bank_selected = 0u;

    SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    if(handle->op_result == SW3538_IO_OK)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_CTRG_WREN,
                                            SW3538_WREN_EXT_BANK));
        if(handle->last_io == SW3538_IO_OK)
            handle->op_bank_selected = 1u;
    }

    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_SYS_POWER_SELECT,
                                           &handle->rxbuf[0]));
    if(handle->op_result == SW3538_IO_OK)
    {
        handle->rxbuf[0] &= (uint8_t)~SW3538_XREG_SYS_PWR_PSET_BIT;
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_SYS_POWER_SELECT,
                                            handle->rxbuf[0]));
    }

    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_PD_CUR_LO,
                                           &handle->rxbuf[0]));
    if(handle->op_result == SW3538_IO_OK)
    {
        handle->rxbuf[0] =
            (uint8_t)((handle->rxbuf[0] & 0x3Fu) |
                      (uint8_t)((handle->op_u16 & 0x03u) << 6));
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_PD20_CUR_HI,
                                            (uint8_t)(handle->op_u16 >> 2)));
    }
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_PD_CUR_LO,
                                            handle->rxbuf[0]));

    /* Never leave bit8 selected after a failed extended-register transaction. */
    if(handle->op_bank_selected != 0u)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_BANK_CLEAR, 0u));
        handle->op_bank_selected = 0u;
    }

    handle->last_io = handle->op_result;
    if(handle->op_result == SW3538_IO_OK)
        handle->status.configured_power_w = watts;
    else
        handle->status.online = 0u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_ProtocolConfigLoad(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;

    if(handle == 0)
        SW3538_RETURN_END();

    handle->op_result = SW3538_IO_OK;
    handle->op_bank_selected = 0u;

    SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    if(handle->op_result == SW3538_IO_OK)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_CTRG_WREN,
                                            SW3538_WREN_EXT_BANK));
        if(handle->last_io == SW3538_IO_OK)
            handle->op_bank_selected = 1u;
    }

    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_DPDM_CFG0,
                                           &handle->protocol_config.raw_dpdm_cfg0));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_DPDM_CFG3,
                                           &handle->protocol_config.raw_dpdm_cfg3));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_DPDM_CFG4,
                                           &handle->protocol_config.raw_dpdm_cfg4));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_PATH_PROTOCOL_CFG,
                                           &handle->protocol_config.raw_path_cfg));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_PD_CFG1,
                                           &handle->protocol_config.raw_pd_cfg1));

    if(handle->op_bank_selected != 0u)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_BANK_CLEAR, 0u));
        handle->op_bank_selected = 0u;
    }

    handle->last_io = handle->op_result;
    if(handle->op_result == SW3538_IO_OK)
        decode_protocol_config(handle);
    else
        handle->status.online = 0u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_SetProtocolMask(SW3538_ARGS(SW3538_ProtocolMask mask))
{
    SW3538_FUNC_BEGIN;

    if(handle == 0 || (mask & (SW3538_ProtocolMask)~SW3538_PROTOCOL_ENABLE_ALL) != 0u)
    {
        if(handle != 0)
            handle->last_io = SW3538_IO_ERROR;
        SW3538_RETURN_END();
    }

    handle->op_result = SW3538_IO_OK;
    handle->op_bank_selected = 0u;

    SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    if(handle->op_result == SW3538_IO_OK)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_CTRG_WREN,
                                            SW3538_WREN_EXT_BANK));
        if(handle->last_io == SW3538_IO_OK)
            handle->op_bank_selected = 1u;
    }

    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_DPDM_CFG0,
                                           &handle->protocol_config.raw_dpdm_cfg0));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_DPDM_CFG3,
                                           &handle->protocol_config.raw_dpdm_cfg3));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_DPDM_CFG4,
                                           &handle->protocol_config.raw_dpdm_cfg4));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_PATH_PROTOCOL_CFG,
                                           &handle->protocol_config.raw_path_cfg));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_PD_CFG1,
                                           &handle->protocol_config.raw_pd_cfg1));

    if(handle->op_result == SW3538_IO_OK)
    {
        if(mask & SW3538_PROTOCOL_ENABLE_DPDM_ALL)
            handle->protocol_config.raw_dpdm_cfg0 |= SW3538_XREG_DPDM_CFG0_BC_ENABLE;
        else
            handle->protocol_config.raw_dpdm_cfg0 &= (uint8_t)~SW3538_XREG_DPDM_CFG0_BC_ENABLE;

        handle->protocol_config.raw_dpdm_cfg3 &=
            (uint8_t)~(SW3538_XREG_DPDM_CFG3_FCP_ENABLE |
                       SW3538_XREG_DPDM_CFG3_SCP_LV_ENABLE |
                       SW3538_XREG_DPDM_CFG3_SCP_HV_ENABLE);
        if(mask & SW3538_PROTOCOL_ENABLE_FCP)
            handle->protocol_config.raw_dpdm_cfg3 |= SW3538_XREG_DPDM_CFG3_FCP_ENABLE;
        if(mask & SW3538_PROTOCOL_ENABLE_SCP)
            handle->protocol_config.raw_dpdm_cfg3 |=
                (SW3538_XREG_DPDM_CFG3_SCP_LV_ENABLE |
                 SW3538_XREG_DPDM_CFG3_SCP_HV_ENABLE);

        handle->protocol_config.raw_dpdm_cfg4 = (uint8_t)(
            ((mask & SW3538_PROTOCOL_ENABLE_PE)   ? SW3538_XREG_DPDM_CFG4_PE_ENABLE   : 0u) |
            ((mask & SW3538_PROTOCOL_ENABLE_SFCP) ? SW3538_XREG_DPDM_CFG4_SFCP_ENABLE : 0u) |
            ((mask & SW3538_PROTOCOL_ENABLE_AFC)  ? SW3538_XREG_DPDM_CFG4_AFC_ENABLE  : 0u) |
            ((mask & SW3538_PROTOCOL_ENABLE_QC2)  ? SW3538_XREG_DPDM_CFG4_QC2_ENABLE  : 0u) |
            ((mask & SW3538_PROTOCOL_ENABLE_QC3)  ? SW3538_XREG_DPDM_CFG4_QC3_ENABLE  : 0u) |
            ((mask & SW3538_PROTOCOL_ENABLE_QC3P) ? SW3538_XREG_DPDM_CFG4_QC3P_ENABLE : 0u) |
            ((mask & SW3538_PROTOCOL_ENABLE_QC4P) ? SW3538_XREG_DPDM_CFG4_QC4P_ENABLE : 0u) |
            ((mask & SW3538_PROTOCOL_ENABLE_TFCP) ? SW3538_XREG_DPDM_CFG4_TFCP_ENABLE : 0u));

        if(mask & SW3538_PROTOCOL_ENABLE_PD)
            handle->protocol_config.raw_pd_cfg1 |= SW3538_XREG_PD_CFG1_ENABLE;
        else
            handle->protocol_config.raw_pd_cfg1 &= (uint8_t)~SW3538_XREG_PD_CFG1_ENABLE;

        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_DPDM_CFG0,
                                            handle->protocol_config.raw_dpdm_cfg0));
    }
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_DPDM_CFG3,
                                            handle->protocol_config.raw_dpdm_cfg3));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_DPDM_CFG4,
                                            handle->protocol_config.raw_dpdm_cfg4));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_PD_CFG1,
                                            handle->protocol_config.raw_pd_cfg1));

    if(handle->op_bank_selected != 0u)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_BANK_CLEAR, 0u));
        handle->op_bank_selected = 0u;
    }

    handle->last_io = handle->op_result;
    if(handle->op_result == SW3538_IO_OK)
        decode_protocol_config(handle);
    else
        handle->status.online = 0u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_SetPortFastChargeEnabled(SW3538_ARGS(uint8_t port, uint8_t enable))
{
    uint8_t bit;

    SW3538_FUNC_BEGIN;

    if(handle == 0 || (port != 1u && port != 2u))
    {
        if(handle != 0)
            handle->last_io = SW3538_IO_ERROR;
        SW3538_RETURN_END();
    }

    /* bit is not used across a yield: copy the selected bit into persistent
     * rx storage before starting I/O. */
    bit = (port == 1u) ? SW3538_XREG_PATH1_FAST_CHARGE_ENABLE
                       : SW3538_XREG_PATH2_FAST_CHARGE_ENABLE;
    handle->rxbuf[3] = bit;
    handle->op_result = SW3538_IO_OK;
    handle->op_bank_selected = 0u;

    SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    if(handle->op_result == SW3538_IO_OK)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_CTRG_WREN,
                                            SW3538_WREN_EXT_BANK));
        if(handle->last_io == SW3538_IO_OK)
            handle->op_bank_selected = 1u;
    }

    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_PATH_PROTOCOL_CFG,
                                           &handle->protocol_config.raw_path_cfg));
    if(handle->op_result == SW3538_IO_OK)
    {
        if(enable != 0u)
            handle->protocol_config.raw_path_cfg |= handle->rxbuf[3];
        else
            handle->protocol_config.raw_path_cfg &= (uint8_t)~handle->rxbuf[3];

        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_PATH_PROTOCOL_CFG,
                                            handle->protocol_config.raw_path_cfg));
    }

    if(handle->op_bank_selected != 0u)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_BANK_CLEAR, 0u));
        handle->op_bank_selected = 0u;
    }

    handle->last_io = handle->op_result;
    if(handle->op_result == SW3538_IO_OK)
    {
        handle->protocol_config.path1_fast_charge_enabled =
            (handle->protocol_config.raw_path_cfg &
             SW3538_XREG_PATH1_FAST_CHARGE_ENABLE) ? 1u : 0u;
        handle->protocol_config.path2_fast_charge_enabled =
            (handle->protocol_config.raw_path_cfg &
             SW3538_XREG_PATH2_FAST_CHARGE_ENABLE) ? 1u : 0u;
    }
    else
    {
        handle->status.online = 0u;
    }

    SW3538_FUNC_END;
}

SW3538_RET SW3538_ProtectionStatusLoad(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;

    if(handle == 0)
        SW3538_RETURN_END();

    /* Normal-bank operational state.  Reg0x20 is read only here; this API does
     * not clear the W/C interrupt flags. */
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                     SW3538_STRG_SYS_STAT0,
                                     &handle->protection.raw_sys_stat0));
    handle->status.sys_stat0 = handle->protection.raw_sys_stat0;
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                     SW3538_STRG_SYS_STAT1,
                                     &handle->protection.raw_sys_stat1));
    handle->status.sys_stat1 = handle->protection.raw_sys_stat1;
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                     SW3538_STRG_IRQ,
                                     &handle->protection.raw_irq));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                     SW3538_CTRG_SYS1,
                                     &handle->protection.raw_sys_ctrl1));

    handle->op_result = SW3538_IO_OK;
    handle->op_bank_selected = 0u;

    SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    if(handle->op_result == SW3538_IO_OK)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_CTRG_WREN,
                                            SW3538_WREN_EXT_BANK));
        if(handle->last_io == SW3538_IO_OK)
            handle->op_bank_selected = 1u;
    }

    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_SINGLE_FAULT_CFG,
                                           &handle->protection.raw_single_fault_cfg));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_TEMP_CFG,
                                           &handle->protection.raw_temp_cfg));
    if(handle->op_result == SW3538_IO_OK)
        SW3538_CAPTURE_IO(SW3538_ByteRead(&handle->io_pt, handle,
                                           SW3538_XREG_VIN_CFG,
                                           &handle->protection.raw_vin_cfg));

    if(handle->op_bank_selected != 0u)
    {
        SW3538_CAPTURE_IO(SW3538_ByteWrite(&handle->io_pt, handle,
                                            SW3538_XREG_BANK_CLEAR, 0u));
        handle->op_bank_selected = 0u;
    }

    handle->last_io = handle->op_result;
    if(handle->op_result == SW3538_IO_OK)
        decode_protection_status(handle);
    else
        handle->status.online = 0u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_ADCRead(SW3538_ARGS(uint8_t channel, uint16_t *raw))
{
    SW3538_FUNC_BEGIN;
    if(raw == 0)
        SW3538_RETURN_END();

    if(adc_force_mask(channel) == 0u)
    {
        handle->last_io = SW3538_IO_ERROR;
        SW3538_RETURN_END();
    }

    /* Make a standalone ADC read self-contained.  VIN sampling is disabled by
     * default, and the other channels may also be gated by the current scene. */
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP1));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP2));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP3));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_CTRG_FORCE2, &handle->rxbuf[0]));
    handle->rxbuf[0] |= adc_force_mask(channel);
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE2, handle->rxbuf[0]));

    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_ADC_SET, channel));
    SW3538_SPAWN_IO(SW3538_BytesRead(&handle->io_pt, handle, SW3538_STRG_ADCL, handle->rxbuf, 2u));
    *raw = decode_adc12(handle->rxbuf);

    SW3538_FUNC_END;
}

SW3538_RET SW3538_ADCLoad(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;

    /* Unlock ordinary writes, then unlock force-operation registers 0x16..0x19. */
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP1));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP2));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_WREN, SW3538_WREN_STEP3));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP1));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP2));
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE_WREN, SW3538_FORCE_WREN_STEP3));

    /* VIN ADC is documented as disabled by default; force all four channels
     * used by this driver on, while preserving unrelated force bits. */
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_CTRG_FORCE2, &handle->rxbuf[0]));
    handle->rxbuf[0] |= (SW3538_CTRG_FORCE2_ADC_VOUT | SW3538_CTRG_FORCE2_ADC_VIN |
                         SW3538_CTRG_FORCE2_ADC_PT1_I | SW3538_CTRG_FORCE2_ADC_PT2_I);
    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_FORCE2, handle->rxbuf[0]));

    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_ADC_SET, SW3538_ADC_SET_VIN));
    SW3538_SPAWN_IO(SW3538_BytesRead(&handle->io_pt, handle, SW3538_STRG_ADCL, handle->rxbuf, 2u));
    handle->status.vin_raw = decode_adc12(handle->rxbuf);
    handle->status.vin_mv = (uint16_t)(handle->status.vin_raw * 10u);

    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_ADC_SET, SW3538_ADC_SET_VOUT));
    SW3538_SPAWN_IO(SW3538_BytesRead(&handle->io_pt, handle, SW3538_STRG_ADCL, handle->rxbuf, 2u));
    handle->status.vout_raw = decode_adc12(handle->rxbuf);
    handle->status.vout_mv = (uint16_t)(handle->status.vout_raw * 6u);

    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_ADC_SET, SW3538_ADC_SET_PT1_IOUT));
    SW3538_SPAWN_IO(SW3538_BytesRead(&handle->io_pt, handle, SW3538_STRG_ADCL, handle->rxbuf, 2u));
    handle->status.pt1_iout_raw = decode_adc12(handle->rxbuf);
    handle->status.pt1_iout_ma_x10 = (uint32_t)handle->status.pt1_iout_raw * 25u;

    SW3538_SPAWN_IO(SW3538_ByteWrite(&handle->io_pt, handle, SW3538_CTRG_ADC_SET, SW3538_ADC_SET_PT2_IOUT));
    SW3538_SPAWN_IO(SW3538_BytesRead(&handle->io_pt, handle, SW3538_STRG_ADCL, handle->rxbuf, 2u));
    handle->status.pt2_iout_raw = decode_adc12(handle->rxbuf);
    handle->status.pt2_iout_ma_x10 = (uint32_t)handle->status.pt2_iout_raw * 25u;

    SW3538_FUNC_END;
}

SW3538_RET SW3538_ProtocolLoad(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_PROTOCOL, &handle->status.protocol));
    SW3538_FUNC_END;
}

SW3538_RET SW3538_PortStatusLoad(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_SYS_STAT0, &handle->status.sys_stat0));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_SYS_STAT1, &handle->status.sys_stat1));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_PT1_ILIM, &handle->status.pt1_ilim_raw));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_PT2_ILIM, &handle->status.pt2_ilim_raw));
    SW3538_FUNC_END;
}

SW3538_RET SW3538_StatusLoad(SW3538_NOARG)
{
    SW3538_FUNC_BEGIN;

    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_REV, &handle->status.version));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_PMAX, &handle->status.pmax_w));
    handle->status.pmax_w &= 0x7Fu;
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_PROTOCOL, &handle->status.protocol));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_SYS_STAT0, &handle->status.sys_stat0));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_SYS_STAT1, &handle->status.sys_stat1));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_PT1_ILIM, &handle->status.pt1_ilim_raw));
    SW3538_SPAWN_IO(SW3538_ByteRead(&handle->io_pt, handle, SW3538_STRG_PT2_ILIM, &handle->status.pt2_ilim_raw));

    SW3538_FUNC_END;
}

const struct SW3538_StatusTypedef *SW3538_GetStatus(const SW3538_Handle *handle)
{
    return (handle != 0) ? &handle->status : 0;
}

const SW3538_ProtocolConfig *SW3538_GetProtocolConfig(const SW3538_Handle *handle)
{
    return (handle != 0) ? &handle->protocol_config : 0;
}

const SW3538_ProtectionStatus *SW3538_GetProtectionStatus(const SW3538_Handle *handle)
{
    return (handle != 0) ? &handle->protection : 0;
}

uint8_t SW3538_IsOnline(const SW3538_Handle *handle)
{
    return (handle != 0) ? handle->status.online : 0u;
}

uint8_t SW3538_GetPortStatus(const SW3538_Handle *handle,
                             uint8_t port,
                             SW3538_PortStatus *status)
{
    if(handle == 0 || status == 0 || (port != 1u && port != 2u))
        return 0u;

    status->online = handle->status.online;

    if(port == 1u)
    {
        status->path_on =
            (handle->status.sys_stat0 & SW3538_STRG_SYS_STAT0_PORT1_ON) ? 1u : 0u;
        status->device_online =
            (handle->status.sys_stat1 & SW3538_STRG_SYS_STAT1_PORT1_ONLINE) ? 1u : 0u;
        status->current_limit_ma = decode_ilim_ma(handle->status.pt1_ilim_raw);
    }
    else
    {
        status->path_on =
            (handle->status.sys_stat0 & SW3538_STRG_SYS_STAT0_PORT2_ON) ? 1u : 0u;
        status->device_online =
            (handle->status.sys_stat1 & SW3538_STRG_SYS_STAT1_PORT2_ONLINE) ? 1u : 0u;
        status->current_limit_ma = decode_ilim_ma(handle->status.pt2_ilim_raw);
    }

    return handle->status.online;
}

uint8_t SW3538_IsProtocolFast(const SW3538_Handle *handle)
{
    return (handle != 0 && (handle->status.protocol & SW3538_STRG_PROTOCOL_FAST) != 0u) ? 1u : 0u;
}

uint8_t SW3538_IsPort1On(const SW3538_Handle *handle)
{
    return (handle != 0 && (handle->status.sys_stat0 & SW3538_STRG_SYS_STAT0_PORT1_ON) != 0u) ? 1u : 0u;
}

uint8_t SW3538_IsPort2On(const SW3538_Handle *handle)
{
    return (handle != 0 && (handle->status.sys_stat0 & SW3538_STRG_SYS_STAT0_PORT2_ON) != 0u) ? 1u : 0u;
}

uint8_t SW3538_IsPort1DeviceOnline(const SW3538_Handle *handle)
{
    return (handle != 0 && (handle->status.sys_stat1 & SW3538_STRG_SYS_STAT1_PORT1_ONLINE) != 0u) ? 1u : 0u;
}

uint8_t SW3538_IsPort2DeviceOnline(const SW3538_Handle *handle)
{
    return (handle != 0 && (handle->status.sys_stat1 & SW3538_STRG_SYS_STAT1_PORT2_ONLINE) != 0u) ? 1u : 0u;
}

uint8_t SW3538_IsBuckOn(const SW3538_Handle *handle)
{
    return (handle != 0 && (handle->status.sys_stat0 & SW3538_STRG_SYS_STAT0_BUCK_ON) != 0u) ? 1u : 0u;
}

uint8_t SW3538_GetPDVersion(const SW3538_Handle *handle)
{
    if(handle == 0)
        return 0u;
    return (uint8_t)((handle->status.protocol & SW3538_STRG_PROTOCOL_PD_VER_MSK) >> SW3538_STRG_PROTOCOL_PD_VER_SHIFT);
}

SW3538_Protocol SW3538_GetProtocol(const SW3538_Handle *handle)
{
    if(handle == 0)
        return SW3538_PROTOCOL_NONE;
    return (SW3538_Protocol)(handle->status.protocol & SW3538_STRG_PROTOCOL_TYPE_MSK);
}

uint16_t SW3538_ReadVINmV(const SW3538_Handle *handle)
{
    return (handle != 0) ? handle->status.vin_mv : 0u;
}

uint16_t SW3538_ReadVOUTmV(const SW3538_Handle *handle)
{
    return (handle != 0) ? handle->status.vout_mv : 0u;
}

uint32_t SW3538_ReadPort1IOUTmA_x10(const SW3538_Handle *handle)
{
    return (handle != 0) ? handle->status.pt1_iout_ma_x10 : 0u;
}

uint32_t SW3538_ReadPort2IOUTmA_x10(const SW3538_Handle *handle)
{
    return (handle != 0) ? handle->status.pt2_iout_ma_x10 : 0u;
}
