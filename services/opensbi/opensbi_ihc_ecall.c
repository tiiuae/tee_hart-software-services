/*******************************************************************************
 * Copyright 2019-2021 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * MPFS Embedded Software
 *
 */


#include "config.h"
#include "hss_types.h"
#include "opensbi_service.h"

#if !IS_ENABLED(CONFIG_OPENSBI)
#  error OPENSBI needed for this module
#endif

#if !IS_ENABLED(CONFIG_USE_IHC)
#  error IHC needed for this module
#endif

#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_version.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_barrier.h>

#include "miv_ihc.h"
#include "opensbi_ecall.h"
#include "opensbi_ihc_ecall.h"

#include "hss_debug.h"

static uint32_t message_present_handler(uint32_t remote_hart_id, uint32_t * message,
    uint32_t message_size , bool is_ack, uint32_t *message_storage_ptr)
{
    struct ihc_sbi_rx_msg msg = { 0 };

    (void)remote_hart_id;

    if (is_ack == true) {
        msg.irq_type = ACK_IRQ;
    } else {
        msg.irq_type = MP_IRQ;
        /* message_size is number of uint32_t elements */
        memcpy((uint32_t *) &msg.ihc_msg, message, message_size * (sizeof(uint32_t)));
    }

    memcpy((uint32_t *) message_storage_ptr, (uint32_t *) &msg, sizeof(struct ihc_sbi_rx_msg));

    return 0u;
}

int sbi_ecall_ihc_handler(unsigned long extid, unsigned long funcid,
    const struct sbi_trap_regs *regs, unsigned long *out_val, struct sbi_trap_info *out_trap)
{
    uint32_t my_hart_id;
    uint32_t remote_hart_id;
    int result = 0;
    uint32_t remote_channel = (uint32_t) regs->a0;
    uint32_t * message_ptr = (uint32_t *) regs->a1;

    if ((remote_channel < IHC_CHANNEL_TO_CONTEXTA) || (remote_channel > IHC_CHANNEL_TO_CONTEXTB)) {
        result = SBI_EINVAL;
        goto exit;
    }

    switch (funcid) {
        case SBI_EXT_IHC_CTX_INIT:
            my_hart_id = IHC_context_to_local_hart_id(remote_channel);
            remote_hart_id = IHC_context_to_remote_hart_id(remote_channel);

#ifdef CONFIG_DEBUG_IHC_VERBOSE_TRACE
            mHSS_DEBUG_PRINTF(LOG_NORMAL, "[%d]: INIT: ch[%d] h[%d]\n",
                        current_hartid(), remote_channel, remote_hart_id);
#endif /* CONFIG_DEBUG_IHC_VERBOSE_TRACE */

            IHC_local_remote_config(my_hart_id, remote_hart_id, message_present_handler, true, true);
            result = SBI_OK;
            break;
        case SBI_EXT_IHC_SEND:
            /* retry sending if receiver busy */
            while(IHC_tx_message(remote_channel, (uint32_t *) message_ptr) == MP_BUSY)
                cpu_relax();

            result = SBI_OK;
            break;
        case SBI_EXT_IHC_RECEIVE:
            IHC_message_present_indirect_isr(current_hartid(), remote_channel, (uint32_t *) message_ptr);
            result = SBI_OK;
            break;
        default:
            result = SBI_ENOTSUPP;
    };

exit:
    if (result >= 0) {
        *out_val = result;
        result = 0;
    }
    return result;
}

