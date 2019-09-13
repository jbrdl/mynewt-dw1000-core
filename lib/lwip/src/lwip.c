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
 * @file dw1000_lwip.c
 * @author paul kettle
 * @date 2018
 * 
 * @brief lwip service
 * @details This is the lwip base class which utilizes the functions to do the configurations related to lwip layer based on dependencies.
 *
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <os/os.h>
#include <hal/hal_spi.h>
#include <hal/hal_gpio.h>
#include "bsp/bsp.h"

#if MYNEWT_VAL(LWIP_ENABLED)

#include <uwb/uwb.h>
#include <uwb/uwb_ftypes.h>
#include <lwip/lwip.h>

#include "sysinit/sysinit.h"


#include <lwip/pbuf.h>
#include <lwip/netif.h>
#include <netif/lowpan6.h>
#include <lwip/ethip6.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>

static bool complete_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs);
static bool rx_complete_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs);
static bool tx_complete_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs);
static bool rx_timeout_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs);
static bool rx_error_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs);
dw1000_lwip_context_t cntxt;
/**
 * API to assign the config parameters.
 *
 * @param inst     Pointer to dw1000_dev_instance_t.
 * @param config   Pointer to structure dw1000_lwip_config_t containing configuration values. 
 * @return struct uwb_dev_status
 */
struct uwb_dev_status
dw1000_lwip_config(dw1000_lwip_instance_t * lwip, dw1000_lwip_config_t * config)
{
	assert(lwip);
	assert(config);

	lwip->config = config;
	return lwip->dev_inst->status;
}

/**
 * API to initialize the lwip service.
 *
 * @param inst     Pointer to dw1000_dev_instance_t.
 * @param config   Pointer to the structure dw1000_lwip_config_t to configure the delay parameters.
 * @param nframes  Number of frames to allocate memory for.
 * @param buf_len  Buffer length of each frame. 
 * @return dw1000_rng_instance_t
 */
dw1000_lwip_instance_t *
dw1000_lwip_init(struct uwb_dev * inst, dw1000_lwip_config_t * config, uint16_t nframes, uint16_t buf_len)
{
	assert(inst);
    dw1000_lwip_instance_t *lwip = (dw1000_lwip_instance_t*)uwb_mac_find_cb_inst_ptr(inst, UWBEXT_LWIP);

	if (lwip == NULL){
		lwip  = (dw1000_lwip_instance_t *) malloc(sizeof(dw1000_lwip_instance_t) + nframes * sizeof(char *));
		assert(lwip);
		memset(lwip,0,sizeof(dw1000_lwip_instance_t) + nframes * sizeof(char *));
		lwip->status.selfmalloc = 1;
		lwip->nframes = nframes;
		lwip->buf_len = buf_len;
		lwip->buf_idx = 0;

		for(uint16_t i=0 ; i < nframes ; ++i){
			lwip->data_buf[i]  = (char *) malloc(sizeof(char)*buf_len);
			assert(lwip->data_buf[i]);
		}
	}
	os_error_t err = os_sem_init(&lwip->sem, 0x01);
	assert(err == OS_OK);
	err = os_sem_init(&lwip->data_sem, nframes);
	assert(err == OS_OK);

	if (config != NULL){
		lwip->config = config;
		dw1000_lwip_config(lwip, config);
	}
    lwip->dev_inst = inst;
    lwip->cbs = (struct uwb_mac_interface){
        .id = UWBEXT_LWIP,
        .inst_ptr = lwip,
        .tx_complete_cb = tx_complete_cb,
        .rx_complete_cb = rx_complete_cb,
        .rx_timeout_cb = rx_timeout_cb,
        .rx_error_cb = rx_error_cb,
		.complete_cb = complete_cb
    };
    uwb_mac_append_interface(inst, &lwip->cbs);

	lwip->status.initialized = 1;
	return lwip;
}

/**
 * API to initialize a PCB for raw lwip.
 *
 * @param   inst Pointer to dw1000_lwip_instance_t.   
 * @return void
 */
void
dw1000_pcb_init(dw1000_lwip_instance_t * lwip)
{
	ip_addr_t ip6_tgt_addr[4];

    IP_ADDR6(ip6_tgt_addr, MYNEWT_VAL(TGT_IP6_ADDR_1), MYNEWT_VAL(TGT_IP6_ADDR_2), 
                            MYNEWT_VAL(TGT_IP6_ADDR_3), MYNEWT_VAL(TGT_IP6_ADDR_4));
	struct raw_pcb * lwip_pcb;
    lwip_pcb = raw_new(IP_PROTO_ICMP);
    raw_bind(lwip_pcb,  lwip->lwip_netif.ip6_addr);
    raw_connect(lwip_pcb, ip6_tgt_addr);
    lwip->pcb = lwip_pcb;
    raw_bind(lwip->pcb,  lwip->lwip_netif.ip6_addr);
	raw_recv(lwip->pcb, lwip_rx_cb, lwip);
}

/**
 * API to mark lwip service as free.
 *
 * @param inst   Pointer to dw1000_lwip_instance_t.
 * @return void
 */
void 
dw1000_lwip_free(dw1000_lwip_instance_t * lwip)
{
	assert(lwip);
	if (lwip->status.selfmalloc)
		free(lwip);
	else
		lwip->status.initialized = 0;
}


/**
 * Received payload is fetched to this function after lwIP stack.
 *
 * @param  arg  User defined argument
 * @param  pcb  Pointer to PCB
 * @param  p    Payload pointer
 * @param  addr Device IP address
 * @return      1: Signifies that the payload is received successfully
 */
uint8_t
lwip_rx_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(addr);
    LWIP_ASSERT("p != NULL", p != NULL);

    dw1000_lwip_instance_t * lwip = (dw1000_lwip_instance_t *)arg;
    if (pbuf_header( p, -PBUF_IP_HLEN)==0){
        lwip->payload_ptr = p->payload;

#if 0
		if(lwip_rx_complete_cb != NULL)
            lwip_rx_complete_cb(inst);
#endif
    }
    memp_free(MEMP_PBUF_POOL,p);
    return 1;
}

/**
 * API to confirm lwip receive complete callback.
 *
 * @param inst  Pointer to dw1000_dev_instance_t.
 * @return void
 */
static bool complete_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs)
{
#if 0
        if(inst->lwip->ext_rx_complete_cb != NULL){
        	inst->lwip->ext_rx_complete_cb(inst);
        }
#endif
	return false;
}

/**
 * API to send lwIP buffer to radio.
 *
 * @param inst  Pointer to dw1000_dev_instance_t.
 * @param p     lwIP Buffer to be sent to radio.
 * @param mode  Represents mode of blocking LWIP_BLOCKING : Wait for Tx to complete LWIP_NONBLOCKING : Don't wait for Tx to complete. 
 * @return struct uwb_dev_status
 */
struct uwb_dev_status 
dw1000_lwip_write(dw1000_lwip_instance_t * lwip, struct pbuf *p, dw1000_lwip_modes_t mode)
{
	/* Semaphore lock for multi-threaded applications */
	os_error_t err = os_sem_pend(&lwip->sem, OS_TIMEOUT_NEVER);
	assert(err == OS_OK);
	assert(p != NULL);

	char *id_pbuf, *temp_buf;
	id_pbuf = (char *)malloc((lwip->buf_len) + 4+2);
	assert(id_pbuf);
	/* Append the 'L' 'W' 'I' 'P' Identifier */
	*(id_pbuf + 0) = 'L';	*(id_pbuf + 1) = 'W';
	*(id_pbuf + 2) = 'I';	*(id_pbuf + 3) = 'P';

	/* Append the destination Short Address */
	*(id_pbuf + 4) = (char)((lwip->dst_addr >> 0) & 0xFF);
	*(id_pbuf + 5) = (char)((lwip->dst_addr >> 8) & 0xFF);

	temp_buf = (char *)p;
	/* Copy the LWIP packet after LWIP Id */
	memcpy(id_pbuf+4+2, temp_buf, lwip->buf_len);

	uwb_write_tx(lwip->dev_inst, (uint8_t *)id_pbuf, 0, lwip->buf_len+4+2);
	free(id_pbuf);
    pbuf_free(p);
    
	uwb_write_tx_fctrl(lwip->dev_inst, lwip->buf_len+4+2, 0);
	lwip->lwip_netif.flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP ;
	lwip->status.start_tx_error = uwb_start_tx(lwip->dev_inst).start_tx_error;

	if( mode == LWIP_BLOCKING )
		err = os_sem_pend(&lwip->sem, OS_TIMEOUT_NEVER); // Wait for completion of transactions units os_clicks
	else
		err = os_sem_pend(&lwip->sem, 500); // Wait for completion of transactions units os_clicks

    if (os_sem_get_count(&lwip->sem) == 0) {
        os_sem_release(&lwip->sem);
    }
	return lwip->dev_inst->status;
}

/**
 * API to put DW1000 radio in Receive mode.
 *
 * @param inst     Pointer to dw1000_dev_instance_t.
 * @param timeout  Timeout value for radio in receive mode. 
 * @return void
 */
void
dw1000_lwip_start_rx(dw1000_lwip_instance_t * lwip, uint16_t timeout)
{
    os_error_t err = os_sem_pend(&lwip->data_sem, OS_TIMEOUT_NEVER);
    assert(err == OS_OK);
    uwb_set_rx_timeout(lwip->dev_inst, timeout);
    uwb_start_rx(lwip->dev_inst);
}

/**
 * API to confirm receive is complete. 
 * 
 * @param inst   Pointer to dw1000_dev_instance_t.
 * @retrun void 
 */
static bool 
rx_complete_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs)
{
    dw1000_lwip_instance_t * lwip = (dw1000_lwip_instance_t *)cbs->inst_ptr;
	if(strncmp((char *)&inst->fctrl, "LW",2))
        return false;

    os_error_t err = os_sem_release(&lwip->data_sem);
    assert(err == OS_OK);

	char *ptr = lwip->data_buf[0];
    memcpy((uint8_t *)ptr, inst->rxbuf, inst->frame_len);

    uint8_t buf_size = lwip->buf_len;
    uint16_t pkt_addr;

    pkt_addr = (uint8_t)(*(lwip->data_buf[0]+4)) + ((uint8_t)(*(lwip->data_buf[0]+5)) << 8);

    if(pkt_addr == lwip->dev_inst->my_short_address){
        char * data_buf = (char *)malloc(buf_size);
        assert(data_buf != NULL);

        memcpy(data_buf,lwip->data_buf[0]+4+2, buf_size);

        struct pbuf * buf = (struct pbuf *)data_buf;
        buf->payload = buf + sizeof(struct pbuf)/sizeof(struct pbuf);

        lwip->lwip_netif.input((struct pbuf *)data_buf, &lwip->lwip_netif);
	}
    else
		dw1000_lwip_start_rx(lwip,0x0000);

	return true;
}

/**
 * API to confirm transmit is complete. 
 *
 * @param inst    Pointer to dw1000_dev_instance_t.
 * @return void
 */
static bool 
tx_complete_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs)
{
    dw1000_lwip_instance_t * lwip = (dw1000_lwip_instance_t *)cbs->inst_ptr;
	if(strncmp((char *)&inst->fctrl, "LW",2)) {
        return false;
    } else if (os_sem_get_count(&lwip->sem) == 0) {
		os_error_t err = os_sem_release(&lwip->sem);
		assert(err == OS_OK);
		return true;
	} else {
		return false;
    }
}

/**
 * API for timeout in receive callback.
 *
 * @param inst   pointer to dw1000_dev_instance_t.
 * @param void
 */
static bool 
rx_timeout_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs)
{
    dw1000_lwip_instance_t * lwip = (dw1000_lwip_instance_t *)cbs->inst_ptr;
    if (os_sem_get_count(&lwip->data_sem) == 0){
		os_error_t err = os_sem_release(&lwip->data_sem);
		assert(err == OS_OK);
		lwip->status.rx_timeout_error = 1;
		return true;
	} else {
		return false;
    }
}

/**
 * API for error in receiving the data.
 *
 * @param inst   pointer to dw1000_dev_instance_t.
 * @return void
 */
static bool 
rx_error_cb(struct uwb_dev * inst, struct uwb_mac_interface * cbs)
{
    dw1000_lwip_instance_t * lwip = (dw1000_lwip_instance_t *)cbs->inst_ptr;

	if (os_sem_get_count(&lwip->data_sem) == 0) {
		os_error_t err = os_sem_release(&lwip->data_sem);
		assert(err == OS_OK);
		lwip->status.rx_error = 1;
        return true;
	} else {
		return false;
    }
}


/**
 * API for radio Low level initialization function. 
 *
 * @param inst         Pointer to dw1000_dev_instance_t.
 * @param txrf_config  Radio Tx and Rx configuration structure.
 * @param mac_config   Radio MAC configuration structure.
 * @return void
 */
void 
dw1000_low_level_init(struct uwb_dev * inst, struct uwb_dev_txrf_config * txrf_config,
                      struct uwb_dev_config * mac_config)
{
	uwb_txrf_config(inst, txrf_config);
	uwb_mac_config(inst, mac_config) ;
}

/**
 * API to configure lwIP network interface.
 *
 * @param inst         Pointer to dw1000_dev_instance_t.
 * @param dw1000_netif Network interface structure to be configured.
 * @param my_ip_addr   IP address of radio.
 * @param rx_status    Default mode status. 
 * @return void
 */
void
dw1000_netif_config(dw1000_lwip_instance_t * lwip, struct netif *dw1000_netif, ip_addr_t *my_ip_addr, bool rx_status)
{
	netif_add(dw1000_netif, NULL, dw1000_netif_init, ip6_input);
	IP_ADDR6_HOST(dw1000_netif->ip6_addr, 	my_ip_addr->addr[0], my_ip_addr->addr[1],
						my_ip_addr->addr[2], my_ip_addr->addr[3]);

	dw1000_netif->ip6_addr_state[0] = IP6_ADDR_VALID;

	netif_set_default(dw1000_netif);
	netif_set_link_up(dw1000_netif);
	netif_set_up(dw1000_netif);

	cntxt.rx_cb.recv = dw1000_lwip_start_rx; 
	lwip->lwip_netif.state = (void*)&cntxt;
	
	if(rx_status)
		dw1000_lwip_start_rx(lwip, 0xffff);
}

/**
 * API to initialise dw1000_netif_init Network interface. 
 *
 * @param dw1000_netif  Network interface structure to be initialized. 
 * @return Error status : Default ERR_OK 
 */
err_t
dw1000_netif_init(struct netif *dw1000_netif){

	LWIP_ASSERT("netif != NULL", (dw1000_netif != NULL));

	dw1000_netif->hostname = "twr_lwip";
	dw1000_netif->name[0] = 'D';
	dw1000_netif->name[1] = 'W';
	dw1000_netif->hwaddr_len = 2;
	dw1000_netif->input = dw1000_ll_input;
	dw1000_netif->linkoutput = dw1000_ll_output;

	return ERR_OK;
}

/**
 * API to pass the payload to lwIP stack.
 *
 * @param inst          Pointer to dw1000_dev_instance_t.
 * @param payload_size  Size of the payload to be sent.
 * @param payload       Pointer to the payload.
 * @param ipaddr        Pointer to the IP address of target device.
 * @return void
 */
void 
dw1000_lwip_send(dw1000_lwip_instance_t *lwip, uint16_t payload_size, char * payload, ip_addr_t * ipaddr)
{
	struct pbuf *pb = pbuf_alloc(PBUF_RAW, (u16_t)payload_size, PBUF_RAM);
	assert(pb != NULL);
	char * payload_lwip = (char *)pb->payload;

	memset(payload_lwip, 0, payload_size);
	memcpy(payload_lwip, payload, payload_size);
    raw_sendto(lwip->pcb, pb, ipaddr);
    pbuf_free(pb);
}

/**
 * Low level output API to bridge 6lowpan and radio.
 *
 * @param dw1000_netif  Network interface.
 * @param p             Buffer to be sent to the radio. 
 * @return Error status
 */
err_t 
dw1000_ll_output(struct netif *dw1000_netif, struct pbuf *p)
{
    struct uwb_dev *udev = uwb_dev_idx_lookup(0);
    dw1000_lwip_instance_t *lwip = (dw1000_lwip_instance_t*)uwb_mac_find_cb_inst_ptr(udev, UWBEXT_LWIP);

	dw1000_lwip_write(lwip, p, LWIP_BLOCKING);

	err_t error = ERR_OK;

	if (lwip->status.request_timeout)
		error = ERR_INPROGRESS;

	if (lwip->status.rx_timeout_error)
		error = ERR_TIMEOUT;

	return error;
}

/**
 * Low level input API to bridge 6lowpan and radio.
 *
 * @param pt            Pointer to received buffer from radio.
 * @param dw1000_netif  Network interface. 
 * @return Error status 
 */
err_t
dw1000_ll_input(struct pbuf *pt, struct netif *dw1000_netif){

	err_t error = ERR_OK;
	pt->payload = pt + sizeof(struct pbuf)/sizeof(struct pbuf);

	error = lowpan6_input(pt, dw1000_netif);
	print_error(error);

	return error;
}

/**
 * API to print error status and type.
 *
 * @param error  Error Type. 
 * @return void
 */
void 
print_error(err_t error){

	switch(error){
		case ERR_MEM :
			printf("[Memory Error]\n");
			break;
		case ERR_BUF :
			printf("[Buffer Error]\n");
			break;
		case ERR_TIMEOUT :
			printf("[Timeout Error]\n");
			break;
		case ERR_RTE :
			printf("[Routing Error]\n");
			break;
		case ERR_INPROGRESS :
			printf("[Inprogress Error]\n");
			break;
		case ERR_OK :
		default :
			break;
	}
}

#endif  /* End MYNEWT_VAL(DW1000_LWIP) */
