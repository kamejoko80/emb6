/**
 *      \addtogroup embetter6
 *      @{
 *      \addtogroup demo
 *      @{
 *      \addtogroup demo_udp_alive
 *      @{
*/
/*
 * emb6 is licensed under the 3-clause BSD license. This license gives everyone
 * the right to use and distribute the code, either in binary or source code
 * format, as long as the copyright license is retained in the source code.
 *
 * The emb6 is derived from the Contiki OS platform with the explicit approval
 * from Adam Dunkels. However, emb6 is made independent from the OS through the
 * removal of protothreads. In addition, APIs are made more flexible to gain
 * more adaptivity during run-time.
 *
 * The license text is:
 *
 * Copyright (c) 2015,
 * Hochschule Offenburg, University of Applied Sciences
 * Laboratory Embedded Systems and Communications Electronics.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*============================================================================*/
/*! \file   demo_udp_alive.c

 \author Peter Lehmann, peter.lehmann@hs-offenburg.de
 \author Artem Yushev, artem.yushev@hs-offenburg.de

 \brief  UDP Client Source for DODAG visualization on Cetic 6LBR

 \version 0.2
 */
/*============================================================================*/

/*==============================================================================
 INCLUDE FILES
 =============================================================================*/

#include "emb6.h"
#include "bsp.h"
#include "demo_udp_alive.h"
#include "etimer.h"
#include "evproc.h"
#include "tcpip.h"
#include "uip.h"
#include "rpl.h"
#include "uip-debug.h"
#include "uip-udp-packet.h"

/*==============================================================================
                                         MACROS
 =============================================================================*/
#define     LOGGER_ENABLE        LOGGER_DEMO_EXUDP
#if            LOGGER_ENABLE     ==     TRUE
#define        LOGGER_SUBSYSTEM    "udp_iaa"
#endif
#include    "logger.h"

/** set the send interval */
#define     SEND_INTERVAL            60

/** set the payload length */
#define     MAX_PAYLOAD_LEN            40

/** set the communication port (default for 6LBR: 3000) */
#define     _PORT                     3000
/** macro for a half of ipv6 address */
#define     HALFIP6ADDR             8
/** macro for a half of ipv6 address */
#define     IP6ADDRSIZE             16

/*==============================================================================
                     TYPEDEF'S DECLARATION
 =============================================================================*/
typedef enum e_udpAliveStateS {
    E_UDPALIVE_UNDEF,
    E_UDPALIVE_GETDESTADDR,
    E_UDPALIVE_CONNECT,
    E_UDPALIVE_PREPMSG,
    E_UDPALIVE_SENDMSG,
    E_UDPALIVE_DONE,
}e_udpAlive_t;

/*==============================================================================
                          LOCAL VARIABLE DECLARATIONS
 =============================================================================*/

static struct uip_udp_conn*  ps_udpDesc = NULL;

static uip_ip6addr_t         s_destAddr;

struct etimer                e_udpAliveTmr;

/*==============================================================================
                               LOCAL FUNCTION PROTOTYPES
 =============================================================================*/

static  int8_t  _udpAlive_sendMsg(void);
static  uint8_t _udpAlive_addAddr(char * pc_buf, const uip_ipaddr_t* rps_addr);

/*==============================================================================
                                    LOCAL FUNCTIONS
 =============================================================================*/

/*----------------------------------------------------------------------------*/
/** \brief  This function appends human readable ipv6 address to outcome string.
 *          0x0010  = 0b0000000000010000
 *           0x0100  = 0b0000000100000000
 *           0x1000  = 0b0001000000000000
 *  \param  pc_buf      Pointer to a data buffer where final string should be
 *                      stored
 *  \param  rps_addr    Pointer to a structure where address is stored
 *
 *  \returns c_resLen   Result length of appended string
 */
/*----------------------------------------------------------------------------*/
static uint8_t _udpAlive_addAddr(char * pc_buf, const uip_ipaddr_t* rps_addr)
{
    uint16_t    i_block; /* 2 bytes in each block */
    uint32_t    i;
    int32_t     j;
    uint8_t     k;
    char*       pc_addrStr = pc_buf;
    uint8_t     c_resLen;

    for (i = 0, j = 0; i < sizeof(uip_ipaddr_t); i += 2) {
        i_block = (rps_addr->u8[i] << 8) + rps_addr->u8[i + 1];
        if (i_block == 0 && j >= 0) {
            if (j++ == 0) {
                pc_addrStr += sprintf(pc_addrStr, "::");
                c_resLen+=2;
            }
        } else {
            if (j > 0) {
                j = -1;
            } else if (i > 0) {
                pc_addrStr += sprintf(pc_addrStr, ":");
                c_resLen+=1;
            }
            pc_addrStr += sprintf(pc_addrStr, "%04x", i_block);
            for (k=1;k<5;k++)
                if (i_block < (0x10<<k)) c_resLen+=1;
        }
    }
    return c_resLen;
}/* _udpAlive_addAddr */

/*----------------------------------------------------------------------------*/
/** \brief  This function is sending message with a sequence number.
 *
 *  \param  none
 *  \param    none
 *
 *  \returns none
 */
/*----------------------------------------------------------------------------*/
static int8_t _udpAlive_sendMsg(void)
{
    LOG_INFO("Try to send message...");

    static uint32_t l_seqId;
    char            ac_buf[MAX_PAYLOAD_LEN];
    uint32_t        i;
    uint8_t         c_err               = 0;
    uint8_t         c_leaveFSM          = 0;
    uint16_t        i_destPort          = UIP_HTONS(_PORT);
    uip_ds6_addr_t* ps_destAddrDesc     = uip_ds6_get_global(ADDR_PREFERRED);
    uip_ipaddr_t*   ps_destAddr         = &ps_destAddrDesc->ipaddr;
    rpl_dag_t*      ps_dagDesc          = rpl_get_any_dag();
    e_udpAlive_t    e_state             = E_UDPALIVE_GETDESTADDR;

    while (!c_leaveFSM) {
        switch (e_state) {
        case E_UDPALIVE_GETDESTADDR:
            if (ps_destAddrDesc != NULL)
            {
                if(ps_dagDesc) {
                    uip_ipaddr_copy(&s_destAddr, ps_destAddr);
                    e_state = E_UDPALIVE_CONNECT;
                }
                else {
                    LOG_ERR("Get parent address FAILED");
                    c_err = -1;
                    e_state = E_UDPALIVE_UNDEF;
                }
            }
            break;
        case E_UDPALIVE_CONNECT:
            if (!ps_udpDesc)
            {
                ps_udpDesc = udp_new(ps_destAddr,i_destPort,NULL);

            } else {
                if (memcmp(&ps_udpDesc->ripaddr, ps_destAddr, IP6ADDRSIZE))
                {
                    uip_udp_remove(ps_udpDesc);
                    ps_udpDesc = udp_new(ps_destAddr,i_destPort,NULL);
                    LOG_WARN("Reconnection to server");
                }
            }

            if (ps_udpDesc) {
                PRINTF("UDPALIVE: Using destination addr:");
                PRINT6ADDR(&ps_udpDesc->ripaddr);
                PRINTF(" local/remote port %u/%u\n",
                       UIP_HTONS(ps_udpDesc->lport),
                       UIP_HTONS(ps_udpDesc->rport));
                e_state = E_UDPALIVE_PREPMSG;
            }
            else {
                e_state = E_UDPALIVE_UNDEF;
                c_err = -2;
                LOG_ERR("Get destination address FAILED\n");
            }

            break;
        case E_UDPALIVE_PREPMSG:
                i = sprintf(ac_buf, "%d | ", ++l_seqId);
                if(ps_dagDesc && ps_dagDesc->instance->def_route) {
                    _udpAlive_addAddr(ac_buf + i,
                                      &ps_dagDesc->instance->def_route->ipaddr);
                } else {
                    sprintf(ac_buf + i, "(null)");
                }
                LOG_INFO("Payload: %s", ac_buf);
                e_state = E_UDPALIVE_SENDMSG;
            break;
        case E_UDPALIVE_SENDMSG:
            uip_udp_packet_send(ps_udpDesc, ac_buf, strlen(ac_buf));
            e_state = E_UDPALIVE_DONE;
            break;
        case E_UDPALIVE_DONE:
        case E_UDPALIVE_UNDEF:
        default:
            c_leaveFSM = 1;
            break;
        }
    }

    return (c_err);

} /* _udpAlive_sendMsg */

/*----------------------------------------------------------------------------*/
/** \brief  This function (UDP Alive) is called whenever the timer expired.
 *
 *  \param  event     Event type
 *  \param    data    Pointer to data
 *
 *  \returns none
 */
/*----------------------------------------------------------------------------*/
static    void _udpAlive_callback(c_event_t c_event, p_data_t p_data) {
    if (etimer_expired(&e_udpAliveTmr))
    {
        _udpAlive_sendMsg();
        etimer_restart(&e_udpAliveTmr);
    }
} /* _udpAlive_callback */

/*=============================================================================
                                         API FUNCTIONS
 ============================================================================*/

/*---------------------------------------------------------------------------*/
/*  demo_udpAliveInit()                                                       /
/*---------------------------------------------------------------------------*/
uint8_t demo_udpAliveConf(s_ns_t* pst_netStack)
{
    uint8_t c_ret = 1;
    /*
     * By default stack
     */
    if (pst_netStack != NULL) {
        if (!pst_netStack->c_configured) {
            pst_netStack->hc     = &sicslowpan_driver;
            pst_netStack->hmac   = &nullmac_driver;
            pst_netStack->lmac   = &sicslowmac_driver;
            pst_netStack->frame  = &framer_802154;
            pst_netStack->c_configured = 1;
            /* Transceiver interface is defined by @ref board_conf function*/
            /* pst_netStack->inif   = $<some_transceiver>;*/
        } else {
            if ((pst_netStack->hc == &sicslowpan_driver)   &&
                (pst_netStack->hmac == &nullmac_driver)    &&
                (pst_netStack->lmac == &sicslowmac_driver) &&
                (pst_netStack->frame == &framer_802154)) {
                /* right configuration */
            }
            else {
                c_ret = 0;
            }
        }
    }
    return c_ret;
}/* demo_udpAliveConf */

/*---------------------------------------------------------------------------*/
/*    demo_udpAliveInit()                                                       /
/*---------------------------------------------------------------------------*/
int8_t demo_udpAliveInit(void)
{
    /* set periodic timer */
    etimer_set( &e_udpAliveTmr,
                SEND_INTERVAL * bsp_get(E_BSP_GET_TRES),
                _udpAlive_callback);
    /* set udp is running */
    return 1;
}/* demo_udpAliveInit()  */
/** @} */
/** @} */
/** @} */
