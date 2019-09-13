/*
 * Copyright 2018, Decawave Limited, All Rights Reserved
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/**
 * @file dw1000_tdma.h
 * @author paul kettle
 * @date 2018
 * @brief TDMA
 *
 * @details  This is the base class of tdma which initialises tdma instance, assigns slots for each node and does ranging continuously based on * addresses.
 */
#ifndef _DW1000_TDMA_H_
#define _DW1000_TDMA_H_

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stats/stats.h>
#include <uwb/uwb.h>
#include <uwb/uwb_ftypes.h>
#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_phy.h>
#include <os/os.h>
#include <uwb_ccp/uwb_ccp.h>

#define TDMA_TASKS_ENABLE

#if MYNEWT_VAL(TDMA_STATS)
STATS_SECT_START(tdma_stat_section)
    STATS_SECT_ENTRY(slot_timer_cnt)
    STATS_SECT_ENTRY(superframe_cnt)
    STATS_SECT_ENTRY(rx_complete)
    STATS_SECT_ENTRY(tx_complete)
STATS_SECT_END
#endif

//! Structure of TDMA
typedef struct _tdma_status_t{
    uint16_t selfmalloc:1;            //!< Internal flag for memory garbage collection
    uint16_t initialized:1;           //!< Instance allocated
    uint16_t awaiting_superframe:1;   //!< Superframe of tdma
}tdma_status_t;

//! Structure of tdma_slot
typedef struct _tdma_slot_t{
    struct _tdma_instance_t * parent;  //!< Pointer to _tdma_instance_ti
    struct hal_timer timer;            //!< Timer
    struct dpl_event event;            //!< Sturcture of event
    uint16_t idx;                      //!< Slot number
    void * arg;                        //!< Optional argument
}tdma_slot_t; 

//! Structure of tdma instance
typedef struct _tdma_instance_t{
    struct uwb_dev * dev_inst;                //!< Pointer to associated uwb_dev
#if MYNEWT_VAL(UWB_CCP_ENABLED)
    struct uwb_ccp_instance * ccp;            //!< Pointer to ccp instance
#endif
#if MYNEWT_VAL(TDMA_STATS)
    STATS_SECT_DECL(tdma_stat_section) stat;  //!< Stats instance
#endif
    tdma_status_t status;                    //!< Status of tdma 
    struct uwb_mac_interface cbs;            //!< MAC Layer Callbacks
    struct dpl_mutex mutex;                  //!< Structure of os_mutex
    uint16_t idx;                            //!< Slot number
    uint16_t nslots;                         //!< Number of slots 
    uint32_t os_epoch;                       //!< Epoch timestamp
    struct dpl_event superframe_event;        //!< Structure of superframe_event
#ifdef TDMA_TASKS_ENABLE
    struct dpl_eventq eventq;                //!< Structure of events
    struct dpl_task task_str;                //!< Structure of tasks
    uint8_t task_prio;                       //!< Priority of tasks
    dpl_stack_t task_stack[DW1000_DEV_TASK_STACK_SZ]   //!< Stack size of each task
        __attribute__((aligned(DPL_STACK_ALIGNMENT)));
#if MYNEWT_VAL(TDMA_SANITY_INTERVAL) > 0
    struct dpl_callout sanity_cb;            //!< Structure of sanity_cb
#endif
#endif
    struct _tdma_slot_t * slot[];           //!< Dynamically allocated slot
}tdma_instance_t;

struct _tdma_instance_t * tdma_init(struct uwb_dev * dev, uint16_t nslots);
void tdma_free(struct _tdma_instance_t * inst);
void tdma_assign_slot(struct _tdma_instance_t * inst, void (* call_back )(struct dpl_event *), uint16_t idx, void * arg);
void tdma_release_slot(struct _tdma_instance_t * inst, uint16_t idx);
void tdma_stop(struct _tdma_instance_t * tdma);

uint64_t tdma_tx_slot_start(struct _tdma_instance_t * tdma, float idx);
uint64_t tdma_rx_slot_start(struct _tdma_instance_t * tdma, float idx);

#ifdef __cplusplus
}
#endif

#endif //_DW1000_TDMA_H_
