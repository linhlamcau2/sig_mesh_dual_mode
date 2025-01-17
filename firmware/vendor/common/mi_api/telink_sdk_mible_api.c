/********************************************************************************************************
 * @file	telink_sdk_mible_api.c
 *
 * @brief	for TLSR chips
 *
 * @author	telink
 * @date	Sep. 30, 2010
 *
 * @par     Copyright (c) 2017, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *          All rights reserved.
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#include "telink_sdk_mible_api.h"
#include "proj_lib/ble/ll/ll_whitelist.h"
#include "proj/common/mempool.h"
#include "proj/mcu/watchdog_i.h"
#include "vendor/common/blt_soft_timer.h"
#if DU_ENABLE
#include "../user_du.h"
#endif
#if MI_API_ENABLE
#include "./libs/mijia_profiles/mi_service_server.h"
#include "./libs/mijia_profiles/stdio_service_server.h"
#include "./libs/common/mible_beacon.h"
#include "./libs/mesh_auth/mible_mesh_auth.h"
#include "common/mible_beacon_internal.h"
#include "mible_log.h"
#include "./libs/gatt_dfu/mible_dfu_main.h"
#include "mi_config.h"
#include "telink_sdk_mesh_api.h"
#include "miio_user_api.h"
#include "iid.h"
void mi_schd_event_handler(schd_evt_t *evt_id);

telink_record_t telink_record;

// call back function ,will be call by the telink api 
u8 telink_ble_mi_app_event(u8 sub_code , u8 *p, int n)
{
	mible_gap_evt_t gap_evt_t = MIBLE_GAP_EVT_ADV_REPORT ;
	mible_gap_evt_param_t gap_para_t;
	gap_para_t.conn_handle =0;
	if(sub_code == HCI_SUB_EVT_LE_ADVERTISING_REPORT){
		gap_evt_t = MIBLE_GAP_EVT_ADV_REPORT;
		mible_gap_adv_report_t *p_report = &(gap_para_t.report);
		event_adv_report_t *pa = (event_adv_report_t *)p;
		if(pa->adr_type){
			p_report->addr_type = MIBLE_ADDRESS_TYPE_RANDOM;
		}else{
			p_report->addr_type = MIBLE_ADDRESS_TYPE_PUBLIC;
		}
		if(pa->event_type == LL_TYPE_ADV_NONCONN_IND){
			p_report->adv_type = ADV_DATA;
		}else if(pa->event_type == LL_TYPE_SCAN_REQ){
			p_report->adv_type = SCAN_RSP_DATA;
		}else{
		    return 1;
		}
		memcpy(p_report->peer_addr,pa->mac,sizeof(pa->mac));
		// memcpy adv data 
		p_report->data_len = pa->len;
		memcpy(p_report->data,pa->data,pa->len);
		// lost rssi;
	
	}else if(sub_code == HCI_SUB_EVT_LE_CONNECTION_COMPLETE){
		gap_evt_t = MIBLE_GAP_EVT_CONNECTED;
		mible_gap_connect_t *p_connect = &(gap_para_t.connect);
		event_connection_complete_t *pc = (event_connection_complete_t *)p;
		memcpy(p_connect->peer_addr,pc->mac,sizeof(pc->mac));
		if(pc->peer_adr_type){
			p_connect->type = MIBLE_ADDRESS_TYPE_RANDOM;
		}else{
			p_connect->type = MIBLE_ADDRESS_TYPE_PUBLIC;
		}
		if(pc->role == LL_ROLE_MASTER){
			p_connect->role = MIBLE_GAP_CENTRAL;
		}else if(pc->role == LL_ROLE_SLAVE){
			p_connect->role = MIBLE_GAP_PERIPHERAL;
		}
		p_connect->conn_param.min_conn_interval = pc->interval;
		p_connect->conn_param.max_conn_interval = pc->interval;
		p_connect->conn_param.slave_latency = pc->latency;
		p_connect->conn_param.conn_sup_timeout = pc->timeout;
	}else if (sub_code == HCI_EVT_DISCONNECTION_COMPLETE){
		gap_evt_t =  MIBLE_GAP_EVT_DISCONNECT;
		mible_gap_disconnect_t *p_dis = &(gap_para_t.disconnect);
		event_disconnection_t *pd = (event_disconnection_t *)p;
		if(pd->reason == HCI_ERR_CONN_TIMEOUT){
			p_dis->reason = CONNECTION_TIMEOUT;
		}else if (pd->reason == HCI_ERR_REMOTE_USER_TERM_CONN){
			p_dis->reason = REMOTE_USER_TERMINATED;
		}else if (pd->reason == HCI_ERR_CONN_TERM_BY_LOCAL_HOST){
			p_dis->reason = LOCAL_HOST_TERMINATED;
		}else{}
		telink_mible_ota_end();
	}else if (sub_code ==  HCI_SUB_EVT_LE_CONNECTION_UPDATE_COMPLETE){
		gap_evt_t = MIBLE_GAP_EVT_CONN_PARAM_UPDATED;
		mible_gap_connect_update_t *p_up = &(gap_para_t.update_conn);
		event_connection_update_t *pu = (event_connection_update_t *)p;
		p_up->conn_param.conn_sup_timeout = pu->timeout;
		p_up->conn_param.max_conn_interval = pu->interval;
		p_up->conn_param.min_conn_interval = pu->interval;
		p_up->conn_param.slave_latency = pu->latency;
	}
	// set all the parameter part ,and call the function interface 
	mible_gap_event_callback(gap_evt_t,&gap_para_t);
	return 1;
}

// callback function ,will call by the telink part 
_attribute_ram_code_ u8 telink_ble_mi_event_callback(u8 opcode,u8 *p)
{
	mible_gatts_evt_t evt = MIBLE_GATTS_EVT_WRITE;
	mible_gatts_evt_param_t evt_param_t;
	memset(&evt_param_t,0,sizeof(mible_gatts_evt_param_t));
	rf_packet_att_readBlob_t *p_r = (rf_packet_att_readBlob_t*)p;
	rf_packet_att_data_t *p_w = (rf_packet_att_data_t*)p;
	mible_gatts_write_t *p_write = &(evt_param_t.write);
	mible_gatts_read_t *p_read = &(evt_param_t.read);
	u16 handle_val=0;
	evt_param_t.conn_handle =0;
	if(opcode == ATT_OP_WRITE_REQ || opcode == ATT_OP_WRITE_CMD ){
		handle_val = (p_w->hh<<8) + p_w->hl;
		if(*(gAttributes[handle_val].p_perm) & ATT_PERMISSIONS_AUTHOR){
			evt = MIBLE_GATTS_EVT_WRITE_PERMIT_REQ;
		}else{
			evt = MIBLE_GATTS_EVT_WRITE;
		}
		p_write->value_handle = handle_val;
		p_write->offset = 0;// the offset of the buffer part 
		p_write->len = (p_w->l2cap)-3;
		p_write->data = p_w->dat;
	}else if (opcode == ATT_OP_READ_REQ){
		handle_val = (p_r->handle1 <<8)+ p_r->handle;
		if(*(gAttributes[handle_val].p_perm) & ATT_PERMISSIONS_AUTHOR){
			evt = MIBLE_GATTS_EVT_READ_PERMIT_REQ;
		}
		p_read->value_handle = handle_val;
	}else if (opcode == ATT_OP_HANDLE_VALUE_CFM){
		evt = MIBLE_GATTS_EVT_IND_CONFIRM;
	}else{
		return 0;
	}
	mible_gatts_event_callback(evt,&evt_param_t);	
	return 1;
}
int telink_ble_mi_event_cb_att(u16 conn, u8 * p)
{
	// push the data into the fifo part,and dispatch the data in the loop part
	rf_packet_l2cap_req_t * req = (rf_packet_l2cap_req_t *)p;
	u8 opcode = req->opcode;
	conn++;
	telink_ble_mi_event_callback(opcode,p);
	return TRUE;
}

// callback function ,and will call by the telink part 
u8 telink_ble_mi_att_sync(u8 att_op,mible_gatts_srv_db_t *p)
{
	mible_arch_event_t evt = MIBLE_GATTS_EVT_WRITE;
	mible_arch_evt_param_t evt_param_t;
	mible_gatts_srv_db_t *p_gatts_db_ini = (evt_param_t.srv_init_cmp.p_gatts_db->p_srv_db);
	if(att_op == 0){
		evt_param_t.srv_init_cmp.status = MI_SUCCESS;
		evt_param_t.srv_init_cmp.p_gatts_db->srv_num =1;
		memcpy((u8 *)p_gatts_db_ini , (u8 *)p,sizeof(mible_gatts_srv_db_t));
		evt = MIBLE_ARCH_EVT_GATTS_SRV_INIT_CMP;
	}else if (att_op == 1){
		evt_param_t.record.status = MI_SUCCESS;
		evt_param_t.record.id = 1;
		evt = MIBLE_ARCH_EVT_RECORD_WRITE;
	}else{
		return 0;
	}
	mible_arch_event_callback(evt, &evt_param_t);
	return 1;
}

// user function ,and will call by the mi part 
mible_status_t telink_ble_mi_get_gap_address(u8 *p_mac)
{
	memcpy(p_mac,tbl_mac,sizeof(tbl_mac));	
	return MI_SUCCESS;
}
// user function ,and will call by the mi part
u8 gap_adv_param_valid(mible_gap_adv_param_t *p_param)
{
	u8 valid =1;
	if(p_param->adv_interval_min > p_param->adv_interval_max){
		valid =0;
	}else if(p_param->adv_interval_min<20 || p_param->adv_interval_min > 0x4000){
		valid =0;
	}else if(p_param->adv_interval_max<20 || p_param->adv_interval_max > 0x4000){
		valid =0;
	}else if(p_param->adv_type < MIBLE_ADV_TYPE_CONNECTABLE_UNDIRECTED || 
		p_param->adv_type > MIBLE_ADV_TYPE_NON_CONNECTABLE_UNDIRECTED	){
		valid =0;
	}
	return valid;
}
mible_status_t telink_ble_mi_adv_start(mible_gap_adv_param_t *p_param)
{
	if(blt_state == BLS_LINK_STATE_CONN){
		return MI_ERR_INVALID_STATE;
	}else if (!gap_adv_param_valid(p_param)){
		return MI_ERR_INVALID_PARAM;
	}
	u8 chn_mask=0;
	if(p_param->ch_mask.ch_37_off){
		chn_mask &=~(BIT(0));
	}else{
		chn_mask |= BIT(0);
	}
	if(p_param->ch_mask.ch_38_off){
		chn_mask &=~(BIT(1));
	}else{
		chn_mask |= BIT(1);
	}
	if(p_param->ch_mask.ch_39_off){
		chn_mask &=~(BIT(2));
	}else{
		chn_mask |= BIT(2);
	}
	bls_ll_setAdvEnable(1);  //adv enable
	if(p_param->adv_interval_min == 16){ //protect the mesh stack ,the interval must be 10ms
		return MI_SUCCESS;
	}
	/*
	// in the mesh ,we not allow to change the parameter of the mesh adv inter.
	status = bls_ll_setAdvParam( p_param->adv_interval_min, p_param->adv_interval_max, \
			 	 	 	 	 	     p_param->adv_type, OWN_ADDRESS_PUBLIC, \
			 	 	 	 	 	     0,  NULL,  chn_mask, ADV_FP_NONE);
	*/
	return MI_SUCCESS;
}
// user function ,and will call by the mi part 
rf_packet_adv_t mi_adv_packet;
void set_adv_mi_certify(rf_packet_adv_t * p,uint8_t const * p_data,
        uint8_t dlen) 
{
	p->header.type = LL_TYPE_ADV_IND;
	memcpy(p->advA,tbl_mac,6);
	memcpy(p->data, p_data, dlen);
	p->rf_len = 6 + dlen;
	p->dma_len = p->rf_len + 2;	
	return ;
}
void set_adv_mi_prehandler(rf_packet_adv_t *p)
{
	memcpy(p,&mi_adv_packet,sizeof(rf_packet_adv_t));
	return ;
}

mible_status_t telink_ble_mi_gap_adv_data_set(uint8_t const * p_data,
        uint8_t dlen, uint8_t const *p_sr_data, uint8_t srdlen)
{
	if (p_data == NULL || dlen>31 ){
		return MI_ERR_INVALID_PARAM;
	}
	//bls_ll_setAdvData((u8 *)p_data,dlen);
	set_adv_mi_certify(&mi_adv_packet,(u8*)p_data,dlen);
	// set the scanrsp for the provision part ;
	bls_ll_setScanRspData((u8 *)p_sr_data,srdlen);
	return MI_SUCCESS;
}
// user function ,and will call by the mi part 

mible_status_t telink_ble_mi_adv_stop()
{
	if(blt_state == BLS_LINK_STATE_CONN){
		return MI_ERR_INVALID_STATE;
	}
	bls_ll_setAdvEnable(0);
	return MI_SUCCESS;
}

u8 mi_update_conn_params_valid(mible_gap_conn_param_t conn_params)
{
	u8 valid =1;
	if(conn_params.max_conn_interval < conn_params.min_conn_interval){
		valid =0;
	}else if(conn_params.max_conn_interval > 0x0C80 || conn_params.max_conn_interval < 0x0006){
		valid =0;
	}else if(conn_params.min_conn_interval > 0x0C80 || conn_params.min_conn_interval < 0x0006){
		valid =0;
	}else if(conn_params.slave_latency > 0x01F3 || conn_params.slave_latency < 0){
		valid =0;
	}else if(conn_params.conn_sup_timeout > 0x0C80 || conn_params.conn_sup_timeout < 0x000A){
		valid =0;
	}
	return valid;
}

// user function ,and will call by the mi part 
mible_status_t telink_ble_mi_update_conn_params(uint16_t conn_handle,mible_gap_conn_param_t conn_params)
{
	if(blt_state != BLS_LINK_STATE_CONN){
		return MI_ERR_INVALID_STATE;
	}else if(!mi_update_conn_params_valid(conn_params)){
		return MI_ERR_INVALID_PARAM;
	}
	conn_handle =0;
	bls_l2cap_requestConnParamUpdate(conn_params.min_conn_interval,conn_params.max_conn_interval,
										conn_params.slave_latency,conn_params.conn_sup_timeout);
	return MI_SUCCESS;
}

// user function ,and will call by the mi api
u16 find_handle_by_uuid_char(u8 mode,u8 *p_uuid ,attribute_t *p_att)
{
	
	for(int i=0;i<p_att->attNum;i++){
		if(mode == MIBLE_UUID_16){
			if(!memcmp(p_uuid,p_att[i].uuid,2) && p_att[i].uuidLen == 2){
				return i;
			}
		}else if(mode == MIBLE_UUID_128){
			if(!memcmp(p_uuid,p_att[i].uuid,16) && p_att[i].uuidLen == 16){
				return i;
			}
		}
	}
	return 0;
}

u16 find_handle_by_uuid_service(u8 mode,u8 *p_uuid ,attribute_t *p_att)
{
	
	for(int i=0;i<p_att->attNum;i++){
		if(mode == MIBLE_UUID_16){
			if(!memcmp(p_uuid,p_att[i].pAttrValue,2) && p_att[i].attrLen == 2){
				return i;
			}
		}else if(mode == MIBLE_UUID_128){
			if(!memcmp(p_uuid,p_att[i].pAttrValue,16) && p_att[i].attrLen == 16){
				return i;
			}
		}
	}
	return 0;
}

mible_status_t telink_ble_mi_gatts_service_init(mible_gatts_db_t *p_server_db)
{
	mible_gatts_srv_db_t *p_srv = p_server_db->p_srv_db ;
	mible_gatts_char_db_t *p_char_db = p_srv->p_char_db ;
	mible_uuid_t *p_uuid_srv_str = &(p_srv->srv_uuid);
	mible_uuid_t *p_uuid_char_str = &(p_char_db->char_uuid);
	u16 handle;
	if(p_srv == NULL || p_char_db == NULL || p_uuid_srv_str == NULL){
		return MI_ERR_INVALID_ADDR;
	}
	
	handle = find_handle_by_uuid_service(p_uuid_srv_str->type,p_uuid_srv_str->uuid128,gAttributes);
	if(handle == 0){
		return MI_ERR_INVALID_PARAM;
	}
	p_srv->srv_handle = handle;
	for(int i=0;i<(p_srv->char_num);i++){
		p_uuid_char_str = &(p_char_db[i].char_uuid);
		handle = find_handle_by_uuid_char(p_uuid_char_str->type,p_uuid_char_str->uuid128,gAttributes);
		if(handle == 0){
			continue;
		}
		p_char_db[i].char_value_handle = handle;
		// set the permit part 
		if(p_char_db[i].wr_author || p_char_db[i].rd_author){
			*(gAttributes[handle].p_perm) |=(ATT_PERMISSIONS_AUTHOR);
		}else{
			*(gAttributes[handle].p_perm) &= (~ATT_PERMISSIONS_AUTHOR);
		}

	}
	return MI_SUCCESS;

}

// user function ,and will call by the mi api
u8 get_uuid_is_character_or_not(u8 *p)
{
	u16 uuid=0;
	uuid = (p[1]<<8)+p[0];
	if(uuid == GATT_UUID_CHARACTER){
		return TRUE;
	}else{
		return FALSE;
	}
}

mible_status_t telink_ble_mi_gatts_value_set(uint16_t srv_handle, uint16_t value_handle,
    uint8_t offset, uint8_t* p_value,
    uint8_t len)
{
	attribute_t * p_att = gAttributes + value_handle;
	attribute_t * p_att_char = gAttributes + value_handle-1;
	srv_handle =0;
	if(p_value == NULL){
		return MI_ERR_INVALID_ADDR;
	}else if (len ==0 ||len > p_att->attrLen){
		return MI_ERR_INVALID_LENGTH;
	}else if (value_handle > gAttributes->attNum ){
		return MIBLE_ERR_ATT_INVALID_ATT_HANDLE;
	}else if (!get_uuid_is_character_or_not(p_att_char->uuid)){
		return MIBLE_ERR_GATT_INVALID_ATT_TYPE;
	}
	memcpy(p_att->pAttrValue,p_value,len);
	return MI_SUCCESS;
}

// user function ,and will call by the mi api 

mible_status_t telink_ble_mi_gatts_value_get(uint16_t srv_handle, uint16_t value_handle,
    uint8_t* p_value, uint8_t *p_len)
{
	attribute_t * p_att = gAttributes + value_handle;
	attribute_t * p_att_char = gAttributes + value_handle-1;
	srv_handle =0;
	if(p_value == NULL || p_value == NULL){
		return MI_ERR_INVALID_ADDR;
	}else if (value_handle > gAttributes->attNum ){
		return MIBLE_ERR_ATT_INVALID_ATT_HANDLE;
	}else if (!get_uuid_is_character_or_not(p_att_char->uuid)){
		return MIBLE_ERR_ATT_INVALID_ATT_HANDLE;
	}
	*p_len = p_att->attrLen;
	memcpy(p_value,p_att->pAttrValue,*p_len);
	return MI_SUCCESS;
}

// user function ,and will call by the mi api 

mible_status_t telink_ble_mi_gatts_notify_or_indicate(uint16_t conn_handle, uint16_t srv_handle,
    uint16_t char_value_handle, uint8_t offset, uint8_t* p_value,
    uint8_t len, uint8_t type)
{
	u8 notify_ret =0;
	conn_handle =0;
	srv_handle =0;
	offset =0;
	attribute_t * p_att_char = gAttributes + char_value_handle-1;
	offset =0;//??
	if(p_value == NULL){
		return MI_ERR_INVALID_ADDR;
	}else if (blt_state != BLS_LINK_STATE_CONN){
		return MI_ERR_INVALID_STATE;
	}else if (len ==0){
		return MI_ERR_INVALID_LENGTH;
	}else if (char_value_handle > gAttributes->attNum ){
		return MIBLE_ERR_ATT_INVALID_ATT_HANDLE;
	}else if (!get_uuid_is_character_or_not(p_att_char->uuid)){
		return MIBLE_ERR_GATT_INVALID_ATT_TYPE;
	}
	if(type == NOTIFY_CMD_CODE){// notification 
		if(blc_ll_getTxFifoNumber()+ 1 >= (blt_txfifo.num - 2 - 3 )){
			return MI_ERR_BUSY;
		}
		notify_ret = bls_att_pushNotifyData(char_value_handle,p_value,len);
	}else if (type ==INDICATION_CMD_CODE ){
		notify_ret = bls_att_pushIndicateData(char_value_handle,p_value,len);		
	}
	if(notify_ret == BLE_SUCCESS){
		return MI_SUCCESS;
	}else{
		return MI_ERR_BUSY;
	}
	
}

ev_time_event_t ev_evnt[EV_TIMER_MAX_CNT];

ev_time_event_t * find_empty_ev_event()
{
	ev_time_event_t *p_ev = NULL;
	for(int i=0;i<EV_TIMER_MAX_CNT;i++){
		if(ev_evnt[i].cb==NULL){
			p_ev = &(ev_evnt[i]);
			return p_ev;
		}
	}
	return p_ev;
}

ev_time_event_t * find_ev_event_by_cb(mible_timer_handler timeout_handler)
{
	ev_time_event_t *p_ev = NULL;
	for(int i=0;i<EV_TIMER_MAX_CNT;i++){
		if(ev_evnt[i].cb==timeout_handler){
			p_ev = &(ev_evnt[i]);
			return p_ev;
		}
	}
	return p_ev;
}

u8 find_timer_id_pointer(void *p_timer_id)
{
	for(int i=0;i<EV_TIMER_MAX_CNT;i++){
		if(&ev_evnt[i]== p_timer_id){
			return TRUE;
		}
	}
	return FALSE;
}

mible_status_t telink_mi_timer_create(void** p_timer_id,
    mible_timer_handler timeout_handler,
    mible_timer_mode mode)
{
	ev_time_event_t *p_ev = NULL;
	p_ev = find_ev_event_by_cb(timeout_handler);
	if(p_ev){// already exist 
		p_ev->interval = 0;
		p_ev->mode = mode;
		*p_timer_id = p_ev;
		return MI_SUCCESS;
	}
	p_ev = find_empty_ev_event();
	if(p_ev != NULL){
			p_ev->cb = timeout_handler;
			p_ev->interval = 0;
			p_ev->mode = mode;
			*p_timer_id = p_ev;
			return MI_SUCCESS;
	}else{
			return MI_ERR_NO_MEM;
		}
}

mible_status_t telink_mi_timer_delete(void *timer_id)
{
	// find the pointer , clear the content part first 
	if(find_timer_id_pointer(timer_id) != TRUE){
		return MI_ERR_INVALID_PARAM;
	}
	ev_unon_timer((ev_time_event_t *) timer_id);
	memset((u8 *)timer_id ,0,sizeof(ev_time_event_t));
	return MI_SUCCESS;
}

mible_status_t telink_mi_timer_start(void* timer_id, uint32_t timeout_value,
    void* p_context)
{
	// find the pointer , clear the content part first 
	if(find_timer_id_pointer(timer_id) != TRUE){
		return MI_ERR_INVALID_PARAM;
	}
	ev_time_event_t *p_ev = (ev_time_event_t *)timer_id;
	p_ev->data = p_context;
	ev_on_timer(p_ev,timeout_value);
	return MI_SUCCESS;
}

mible_status_t telink_mi_timer_stop(void* timer_id)
{
	// delete from the list table 
	if(find_timer_id_pointer(timer_id) != TRUE){
		return MI_ERR_INVALID_PARAM;
	}
	ev_unon_timer((ev_time_event_t *) timer_id);
	return MI_SUCCESS;
}

// record dispatch part 
u32 flash_idx_adr = FLASH_ADR_MI_RECORD;
u32 flash_idx_adr_cpy = FLASH_ADR_MI_RECORD_TMP;
void telink_record_part_init()
{
	
	uint8_t *p_buf = (u8 *)(&telink_record);
	u32 record_adr = FLASH_ADR_MI_RECORD;
	while(1){
		flash_read_page(record_adr,sizeof(telink_record_t),p_buf);
		if(telink_record.rec_id == 0xffff || record_adr >= FLASH_ADR_MI_RECORD_TMP){
			flash_idx_adr = record_adr;
			break;
		}
		record_adr += telink_record.len +3 ;
	}
}

u8 mi_ota_downing =0;
unsigned char  mi_ota_is_busy()
{
	if(mi_ota_downing){
		return 1;
	}else{
		return 0;
	}
}

void mible_dfu_handler(mible_dfu_state_t state, mible_dfu_param_t *param)
{   
#define MIBLE_DFU_STATUS_SUCC                                  0x00
#define MIBLE_DFU_STATUS_ERR_NO_CMD                            0x01
#define MIBLE_DFU_STATUS_ERR_AUTH_FAIL                         0x02
#define MIBLE_DFU_STATUS_ERR_INVALID                           0x03
#define MIBLE_DFU_STATUS_ERR_NO_MEM                            0x04
#define MIBLE_DFU_STATUS_ERR_BUSY                              0x05
#define MIBLE_DFU_STATUS_ERR_UNSIGNED                          0x06
#define MIBLE_DFU_STATUS_ERR_RX_FAIL                           0x07
#define MIBLE_DFU_STATUS_ERR_LOW_BATTERY                       0x08
#define MIBLE_DFU_STATUS_ERR_UNKNOWN                           0xFF

#define RECORD_DFU_INFO                                        5

    if(MIBLE_DFU_STATE_START == state){
		#if (ZBIT_FLASH_WRITE_TIME_LONG_WORKAROUND_EN)
		check_and_set_1p95v_to_zbit_flash();
		#endif
        MI_LOG_INFO("state = MIBLE_DFU_STATE_START\n");
		mi_ota_downing =1;
    }
    else if(MIBLE_DFU_STATE_VERIFY == state){
      	MI_LOG_INFO("state = MIBLE_DFU_STATE_VERIFY\n");
		mi_ota_downing =0;
		
       if(MIBLE_DFU_STATUS_SUCC == param->verify.value){
          	MI_LOG_INFO("value = MIBLE_DFU_VERIFY_SUCC\n");
			// in this mode ,it will control reset automatic
       }else if(MIBLE_DFU_STATUS_ERR_AUTH_FAIL == param->verify.value){
          	MI_LOG_INFO("value = MIBLE_DFU_VERIFY_FAIL\n");
			// set flag and wait to reset ,when disconnect 
			mible_reboot();
			
       }
    }else if(MIBLE_DFU_STATE_SWITCH == state){
        MI_LOG_INFO("state = MIBLE_DFU_STATE_SWITCH\n");

    }else if(MIBLE_DFU_STATE_CANCEL == state){
        MI_LOG_INFO("state = MIBLE_DFU_STATE_CANCEL\n");
		mible_reboot();
    }
    /*
    else if(MIBLE_DFU_STATE_WRITEERR == state){
        MI_LOG_INFO("state = MIBLE_DFU_STATE_WRITEERR\n");
        mible_record_delete(RECORD_DFU_INFO);
		// set flag and wait to reset ,when disconnect 
		reboot_flag =1;        
    }*/
}

void stdio_rx_handler(uint8_t* p, uint8_t l)
{
	int errno;
	/* RX plain text (It has been decrypted) */
	MI_LOG_INFO("RX raw data\n");
	MI_LOG_HEXDUMP(p, l);
	
	/* TX plain text (It will be encrypted before send out.) */
	errno = stdio_tx(p, l);
	MI_LOG_INFO("errno = %d\n", errno);
	MI_ERR_CHECK(errno);
}

#if HAVE_MSC
const iic_config_t iic_config = {
    .scl_pin  = GPIO_MSC_SCL,
    .sda_pin  = GPIO_MSC_SDA,
    .freq     = I2C_MSC_FREQ,
};

int mijia_secure_chip_power_manage(bool power_stat)
{
    if (power_stat == 1) {
        gpio_write(GPIO_MSC_RESET,1);
        gpio_set_output_en(GPIO_MSC_RESET,1);   
    } else {
        gpio_write(GPIO_MSC_RESET,0);
        gpio_set_output_en(GPIO_MSC_RESET,1);  
    }
    return 0;
}

mible_libs_config_t msc_config = {
    .msc_onoff        = mijia_secure_chip_power_manage,
    .p_msc_iic_config = (void*)&iic_config
};
#endif

void mi_config_init()
{
//extern unsigned int ota_program_offset;
//extern unsigned int  ota_firmware_size_k;
//#define DFU_NVM_START          (ota_program_offset)
//#define DFU_NVM_SIZE           (ota_firmware_size_k<<10)
	m_config.dfu_start = (ota_program_offset);
	m_config.dfu_size = (ota_firmware_size_k<<10);
}

#if USE_GATT_SPEC

static const uint8_t support_devinfo[] = {
    //DEV_INFO_MCU_VERSION,
    DEV_INFO_DEVICE_SN,
    DEV_INFO_HARDWARE_VERSION,
    DEV_INFO_LATITUDE,
    DEV_INFO_LONGITUDE,
    DEV_INFO_VENDOR1,
    DEV_INFO_VENDOR2,
};

static void user_devinfo_callback(dev_info_type_t type, dev_info_t* buf)
{
    switch(type){
    case DEV_INFO_SUPPORT:
		MI_LOG_INFO("user_devinfo_callback DEV_INFO_SUPPORT",0);
        buf->len = sizeof(support_devinfo);
        memcpy(buf->buff, support_devinfo, buf->len);
        break;
    case DEV_INFO_DEVICE_SN:
        buf->len = strlen("12345/A8Q600002");
        memcpy(buf->buff, "12345/A8Q600002", buf->len);
        break;
    case DEV_INFO_HARDWARE_VERSION:
        buf->len = strlen("TLSR825X.Dvt.2");
        memcpy(buf->buff, "TLSR825X.Dvt.2", buf->len);
        break;
    case DEV_INFO_VENDOR1:
        buf->len = strlen("VendorData.1");
        memcpy(buf->buff, "VendorData.1", buf->len);
        break;
    case DEV_INFO_VENDOR2:
        buf->len = strlen("VendorData.2");
        memcpy(buf->buff, "VendorData.2", buf->len);
        break;
    case DEV_INFO_LATITUDE:
        buf->len = strlen("116.397128");
        memcpy(buf->buff, "116.397128", buf->len);
        break;
    case DEV_INFO_LONGITUDE:
        buf->len = strlen("39.916527");
        memcpy(buf->buff, "39.916527", buf->len);
        break;
    default:
        buf->code = MI_ERR_NOT_FOUND;
        return;
    }
    buf->code = MI_SUCCESS;
    return;
}

#endif
static int user_state_process(uint8_t event, void *data)
{
	switch(event){
	    case MIBLE_USER_PROV_CB_TYPE_UNPROV:
			 break;
		case MIBLE_USER_PROV_CB_TYPE_PROVED:
			 break;
		default:
			 break;
	}
	return 0;
}

void telink_mi_vendor_init()
{
	mi_config_init();
	mible_mesh_device_init();
	stdio_service_init(stdio_rx_handler);
	miio_dfu_callback_register(mible_dfu_handler);
	mible_mesh_event_callback(MIBLE_MESH_EVENT_STACK_INIT_DONE, NULL);
    process_mesh_node_init_event();
	#if USE_GATT_SPEC
	miio_gatt_spec_init(on_property_set, on_property_get, on_action_invoke,32);
	miio_mesh_user_callback_register(on_property_set, on_property_get, on_action_invoke, user_state_process);
	miio_system_info_callback_register(user_devinfo_callback);
	property_para_init();
	#endif
}

u8 telink_write_flash(u32 *p_adr,u8 *p_buf,u8 len )
{
	u32 tmp_adr = *p_adr;
	flash_write_page(tmp_adr,len,p_buf);
	tmp_adr += len;
	*p_adr = tmp_adr;
	return TRUE;
}

u8 telink_record_clean_cpy()
{
	u32 record_adr = FLASH_ADR_MI_RECORD;
	u32 record_adr_cpy = FLASH_ADR_MI_RECORD_TMP;
	u32 cpy_cnt =0;
	uint8_t *p_buf = (u8 *)(&telink_record);
	flash_erase_sector(FLASH_ADR_MI_RECORD_TMP);
	while(1){//cpy the record to tmp first 
		flash_read_page(record_adr,sizeof(telink_record_t),p_buf);
		if(telink_record.rec_id == 0xffff || record_adr >= FLASH_ADR_MI_RECORD_MAX){
			break;
		}else if(telink_record.rec_id == 0){
			record_adr += telink_record.len +3 ;
		}else{
			// need to cpy
			u8 total_len =0;
			total_len = telink_record.len+3;
			while(total_len){
				if(total_len > sizeof(telink_record_t)){
					telink_write_flash(&record_adr_cpy,p_buf,sizeof(telink_record_t));
					total_len -= sizeof(telink_record_t);
					record_adr += sizeof(telink_record_t);
					flash_read_page(record_adr,sizeof(telink_record_t),p_buf);
				}else{
					telink_write_flash(&record_adr_cpy,p_buf,total_len);
					record_adr += total_len;
					total_len =0;
				}
			}
		}
	}
	flash_erase_sector(FLASH_ADR_MI_RECORD);
	cpy_cnt = record_adr_cpy - FLASH_ADR_MI_RECORD_TMP;
	flash_idx_adr = FLASH_ADR_MI_RECORD + cpy_cnt;// update the write adr part 
	u32 idx =0;
	while(cpy_cnt){
		if(cpy_cnt > sizeof(telink_record_t)){
			flash_read_page(FLASH_ADR_MI_RECORD_TMP+idx,sizeof(telink_record_t),p_buf);
			flash_write_page(FLASH_ADR_MI_RECORD+idx,sizeof(telink_record_t),p_buf);
			idx += sizeof(telink_record_t);
			cpy_cnt -= sizeof(telink_record_t);
		}else{
			flash_read_page(FLASH_ADR_MI_RECORD_TMP+idx,cpy_cnt,p_buf);
			flash_write_page(FLASH_ADR_MI_RECORD+idx,cpy_cnt,p_buf);
			cpy_cnt = 0;
		}
	}	
	return TRUE;
}

mible_status_t telink_record_create(uint16_t record_id, uint8_t len)
{
	return MI_SUCCESS;
}


mible_status_t telink_record_delete(uint16_t record_id)
{
	u32 record_adr =0;
	uint8_t *p_buf = (u8 *)(&telink_record);
	if(!find_record_adr(record_id,&record_adr)){
		telink_record_eve_cb(record_id,MI_ERR_INVALID_PARAM,MIBLE_ARCH_EVT_RECORD_DELETE);
		return MI_ERR_INVALID_PARAM;
	}
	//flash_read_page(record_adr,sizeof(telink_record_t),p_buf);
	telink_record.rec_id =0;
	// set the record id to 0 
	flash_write_page(record_adr,2,p_buf);//just clear the rec id part 
	telink_record_eve_cb(record_id,MI_SUCCESS,MIBLE_ARCH_EVT_RECORD_DELETE);
	return MI_SUCCESS;
}
uint8_t buf_is_empty_or_not(uint8_t* p_data,uint8_t len)
{
	for(int i=0;i<len;i++){
		if(p_data[i] != 0){
			return 0;
		}
	}
	return 1;
}
mible_status_t telink_record_read(uint16_t record_id, uint8_t* p_data,uint8_t len)
{
	u32 record_adr =0;
	uint8_t *p_buf = (u8 *)(&telink_record);
	if(len > RECORD_MAX_LEN ||len ==0  ){
		return  MI_ERR_INVALID_LENGTH;
	}else if(p_data == NULL){
		return MI_ERR_INVALID_ADDR;
	}else if(!find_record_adr(record_id,&record_adr)){
		return MI_ERR_INVALID_PARAM;
	}
	flash_read_page(record_adr,sizeof(telink_record_t),p_buf); 
	if(len>telink_record.len){
		return MI_ERR_INVALID_LENGTH;
	}
	if(len<=sizeof(telink_record.dat)){
		memcpy(p_data,telink_record.dat,len);
		/*
		if(buf_is_empty_or_not(p_data,len)){
			return MIBLE_ERR_UNKNOWN;
		}*/
		return MI_SUCCESS;
	}else{
		// directly read all the buf part 
		flash_read_page(record_adr+3,len ,p_data);
		/*
		if(buf_is_empty_or_not(p_data,len)){
			return MIBLE_ERR_UNKNOWN;
		}*/
		return MI_SUCCESS;
	}
}
void telink_record_eve_cb(uint16_t record_id,mible_status_t sts,mible_arch_event_t eve)
{
	mible_arch_evt_param_t param;
	param.record.status =sts;
	param.record.id = record_id;
	mible_arch_event_callback(eve, &param);
	return ;
}
mible_status_t telink_record_write(uint16_t record_id, uint8_t* p_data,uint8_t len)
{
	uint8_t total_len = len;
	uint8_t buf_idx =0;
	uint8_t *p_buf = (u8 *)(&telink_record);
	if(len > RECORD_MAX_LEN || len == 0){
		telink_record_eve_cb(record_id,MI_ERR_INVALID_LENGTH,MIBLE_ARCH_EVT_RECORD_WRITE);
		return MI_ERR_INVALID_LENGTH;
	}
	// check the first record 
	telink_record_delete(record_id);
	if(flash_idx_adr + (len+3)+RECORD_RESERVE_SPACE > FLASH_ADR_MI_RECORD_TMP){
		// need to clean the flash first .
		MI_LOG_INFO("start record clean %d\n",flash_idx_adr);
		telink_record_clean_cpy();
		MI_LOG_INFO("start record end %d \n",flash_idx_adr);
	}
	// write part 
	memset(p_buf,0,sizeof(telink_record_t));
	telink_record.rec_id = record_id;
	telink_record.len = total_len;
	// write the header part 
	if(total_len > sizeof(telink_record.dat)){
		memcpy(telink_record.dat,p_data,sizeof(telink_record.dat));
		telink_write_flash(&flash_idx_adr,p_buf,sizeof(telink_record_t));
		total_len -= sizeof(telink_record.dat);
		buf_idx += sizeof(telink_record.dat);
	}else{
		memcpy(telink_record.dat,p_data,total_len);
		telink_write_flash(&flash_idx_adr,p_buf,total_len+3);
		total_len = 0;
	}
	// write the continue part 
	while(total_len >0){
		memset(p_buf,0,sizeof(telink_record_t));
		if(total_len>sizeof(telink_record_t)){
			memcpy(p_buf,p_data+buf_idx,sizeof(telink_record_t));
			telink_write_flash(&flash_idx_adr,p_buf,sizeof(telink_record_t));
			total_len -= sizeof(telink_record_t);
			buf_idx += sizeof(telink_record_t);
		}else{
			memcpy(p_buf,p_data+buf_idx,total_len);
			telink_write_flash(&flash_idx_adr,p_buf,total_len);
			total_len =0;
			buf_idx =0;
		}
	}
	telink_record_eve_cb(record_id,MI_SUCCESS,MIBLE_ARCH_EVT_RECORD_WRITE);
	return MI_SUCCESS;
}

_attribute_ram_code_ uint8_t find_record_adr(uint16_t record_id,u32 *p_adr) // make the record find fun more faster
{
	u32 idx =0;
	u8 reocrd_buf[512];
	u32 record_idx =0;
	telink_record_t *p_record = (telink_record_t *)reocrd_buf;
	flash_read_page(FLASH_ADR_MI_RECORD+idx,sizeof(reocrd_buf),reocrd_buf);
	while((idx + RECORD_RESERVE_SPACE)<4096){
		if((record_idx+3 )> sizeof(reocrd_buf)){
			flash_read_page(FLASH_ADR_MI_RECORD+idx,sizeof(reocrd_buf),reocrd_buf);
			record_idx = 0;
		}
		p_record = (telink_record_t *)(reocrd_buf+record_idx);
		if(p_record->rec_id == 0xffff){
			return FALSE;
		}else if (p_record->rec_id == record_id){
			*p_adr = FLASH_ADR_MI_RECORD+idx;
			return TRUE;
		}else{
			idx +=p_record->len+3;
			record_idx +=p_record->len+3;
		}
	}
	return FALSE;
}




// user function ,and will call by the mi part 
mible_status_t telink_ble_mi_aes128_encrypt(const uint8_t* key,
    const uint8_t* plaintext, uint8_t plen,
    uint8_t* ciphertext)
{
	if(key==NULL || plaintext ==NULL || ciphertext== NULL){
		return MI_ERR_INVALID_ADDR;
	}
	#if 0
	int plen_aes =0;
	plen_aes = plen;
	tn_aes_cmac ( (u8 *)key, (u8 *)plaintext, plen_aes,ciphertext );
	#else
	// raw data aes part 
	u8 text_dat[16];
	memset(text_dat,0,sizeof(text_dat));
	memcpy(text_dat,plaintext,plen);
	tn_aes_128((u8*)key, text_dat, ciphertext);
	#endif 
	return MI_SUCCESS;
}


// task poll part 
MYFIFO_INIT(mi_task_api_func_t, 16, 8);
/*
 * @brief   Post a task to a task quene, which can be executed in a right place(maybe a task in RTOS or while(1) in the main function).
 * @param   [in] handler: a pointer to function 
 *          [in] param: function parameters 
 * @return  MI_SUCCESS              Successfully put the handler to quene.
 *          MI_ERR_NO_MEM           The task quene is full. 
 *          MI_ERR_INVALID_PARAM    Handler is NULL
 * */

mible_status_t telink_mi_task_post(mible_handler_t handler, void *arg)
{
	mi_task_fun_t task_fun;
	task_fun.cb = handler;
	task_fun.arg = arg;
	if(handler == NULL){
		return MI_ERR_INVALID_PARAM;
	}
	if(my_fifo_push(&mi_task_api_func_t,(u8*)(&task_fun),sizeof(mi_task_fun_t),0,0)!=0){
		return MI_ERR_NO_MEM;
	}else{
		return MI_SUCCESS;
	}
}
mible_status_t telink_mi_task_exec()
{
	// pull the function pointer part 
	mi_task_fun_t task_fun;
	u8 *p_func;
	p_func = my_fifo_get(&mi_task_api_func_t);
	if(p_func!=NULL){
		my_fifo_pop(&mi_task_api_func_t);
		memcpy((u8 *)(&task_fun),p_func+2,sizeof(mi_task_fun_t));
		task_fun.cb(task_fun.arg);
	}
	return MI_SUCCESS;
}
void telink_mible_ota_start()
{}

void telink_mible_ota_end()
{}

void telink_mible_nvm_write(void * p_data, uint32_t length, uint32_t address)
{
	if(address == ota_program_offset){// means ota start 
		telink_mible_ota_start();
	}
	flash_write_page(address,length,(u8 *)p_data);
}

void telink_mible_upgrade_firmware(void)
{
	ota_set_flag ();// set flag and 
	start_reboot();
}

// set the button part 
void mi_testboard_init()
{
	gpio_set_func(GPIO_PD2,AS_GPIO);
	gpio_set_input_en(GPIO_PD2,1);// wait to detect
	gpio_setup_up_down_resistor(GPIO_PD2,PM_PIN_PULLUP_1M);
	// line on the led part 
	gpio_set_func(GPIO_PC4,AS_GPIO);
	gpio_set_input_en(GPIO_PC4,1);
	gpio_set_output_en(GPIO_PC4,1);
	gpio_write(GPIO_PC4,0);
}
void mi_detect_reset_proc()
{
	// demo code to read the io part 
	if(!gpio_read(GPIO_PD2)){
		// reset status 
		kick_out(1);
	}else{
	}	
}

u8 mi_api_loop_run()
{
	mible_tasks_exec();// must
	mible_mesh_device_main_thread();// must
	return TRUE;
}

// malloc free calloc proc part 
#define DEFAULT_BUFFER_GROUP_NUM                 3

#define BUFFER_GROUP_0                   64+4
#define BUFFER_GROUP_1                   256+4
#define BUFFER_GROUP_2                   512+4
#define MAX_BUFFER_SIZE                  BUFFER_GROUP_2

#define BUFFER_NUM_IN_GROUP0             32
#define BUFFER_NUM_IN_GROUP1             4
#define BUFFER_NUM_IN_GROUP2             2

MEMPOOL_DECLARE(size_0_pool, size_0_mem, BUFFER_GROUP_0, BUFFER_NUM_IN_GROUP0);
MEMPOOL_DECLARE(size_1_pool, size_1_mem, BUFFER_GROUP_1, BUFFER_NUM_IN_GROUP1);
MEMPOOL_DECLARE(size_2_pool, size_2_mem, BUFFER_GROUP_2, BUFFER_NUM_IN_GROUP2);

mem_pool_t *memPool[DEFAULT_BUFFER_GROUP_NUM] = {&size_0_pool, &size_1_pool, &size_2_pool};

void mem_pool_init(void)
{
	u16 size[DEFAULT_BUFFER_GROUP_NUM] = {BUFFER_GROUP_0, BUFFER_GROUP_1, BUFFER_GROUP_2};
    u8 *mem[DEFAULT_BUFFER_GROUP_NUM] = {size_0_mem, size_1_mem, size_2_mem};
    u8 buffCnt[DEFAULT_BUFFER_GROUP_NUM] = {BUFFER_NUM_IN_GROUP0, BUFFER_NUM_IN_GROUP1, BUFFER_NUM_IN_GROUP2};
	
/* reinitialize available buffer */
    for(u8 i = 0; i < DEFAULT_BUFFER_GROUP_NUM; i++){
        mempool_init(memPool[i], mem[i], size[i], buffCnt[i]);
    }  
}

int get_mem_pool_idx_by_size(u32 size)
{
	size+=4;
	if(size>BUFFER_GROUP_2){
		return -1;
	}else if (size>BUFFER_GROUP_1){
		return 2;
	}else if (size>BUFFER_GROUP_0){
		return 1;
	}else{
		return 0;
	}
}

void* malloc(size_t size)
{
	int idx = get_mem_pool_idx_by_size(size);
	if(idx == -1){
		return 0;
	}
	char *p_char = mempool_alloc(memPool[idx]);
	return p_char;
}

void* calloc(size_t num,size_t size)
{
	u32 buf_cnt = num*size;
	int idx = get_mem_pool_idx_by_size(buf_cnt);
	if(idx == -1){
		return 0;
	}
	char * p_buf = mempool_alloc(memPool[idx]);
	if(p_buf !=0){
		memset(p_buf,0,buf_cnt);
	}
	return p_buf;
}
// use the pointer to get the pool num 

int get_mem_pool_idx_by_pointer(void *arg){
	if((u32)arg >= (u32)(size_0_mem) && (u32)arg <= ((u32)(size_0_mem) + sizeof(size_0_mem))){
		return 0;
	}else if ((u32)arg >= (u32)(size_1_mem) && (u32)arg <= ((u32)(size_1_mem) + sizeof(size_1_mem))){
		return 1;
	}else if ((u32)arg >= (u32)(size_2_mem) && (u32)arg <= ((u32)(size_2_mem) + sizeof(size_2_mem))){
		return 2;
	}else{
		return -1;
	}
}

void free(void *FirstByte)
{
	int idx = get_mem_pool_idx_by_pointer(FirstByte);
	if(idx == -1){
		return ;
	}
	mempool_free(memPool[idx],FirstByte);
}

#endif 

// ecc fun proc 
#define CHAR_BIT	8
typedef unsigned long mp_limb_t;
typedef long mp_size_t;
typedef unsigned long mp_bitcnt_t;

typedef mp_limb_t *mp_ptr;
typedef const mp_limb_t *mp_srcptr;

#define GMP_LIMB_BITS (sizeof(mp_limb_t) * CHAR_BIT)

#define GMP_LIMB_MAX (~ (mp_limb_t) 0)
#define GMP_LIMB_HIGHBIT ((mp_limb_t) 1 << (GMP_LIMB_BITS - 1))

#define GMP_HLIMB_BIT ((mp_limb_t) 1 << (GMP_LIMB_BITS / 2))
#define GMP_LLIMB_MASK (GMP_HLIMB_BIT - 1)

#define GMP_ULONG_BITS (sizeof(unsigned long) * CHAR_BIT)
#define GMP_ULONG_HIGHBIT ((unsigned long) 1 << (GMP_ULONG_BITS - 1))


// use  hardware mul32x32_64 to accelerate
#define gmp_umul_ppmm(w1, w0, u, v)					\
  do {												\
    mp_limb_t __x0, __x1, __x2, __x3;				\
    unsigned __ul, __vl, __uh, __vh;				\
    mp_limb_t __u = (u), __v = (v);					\
													\
    __ul = __u & GMP_LLIMB_MASK;					\
    __uh = __u >> (GMP_LIMB_BITS / 2);				\
    __vl = __v & GMP_LLIMB_MASK;					\
    __vh = __v >> (GMP_LIMB_BITS / 2);				\
													\
    __x0 = (mp_limb_t) __ul * __vl;					\
    __x1 = (mp_limb_t) __ul * __vh;					\
    __x2 = (mp_limb_t) __uh * __vl;					\
    __x3 = (mp_limb_t) __uh * __vh;					\
													\
    __x1 += __x0 >> (GMP_LIMB_BITS / 2);/* this can't give carry */		\
    __x1 += __x2;		/* but this indeed can */	\
    if (__x1 < __x2)		/* did we get it? */	\
      __x3 += GMP_HLIMB_BIT;	/* yes, add it in the proper pos. */	\
													\
    (w1) = __x3 + (__x1 >> (GMP_LIMB_BITS / 2));	\
    (w0) = (__x1 << (GMP_LIMB_BITS / 2)) + (__x0 & GMP_LLIMB_MASK);		\
  } while (0)


#define gmp_add_ssaaaa(sh, sl, ah, al, bh, bl) 		\
	  do {											\
		mp_limb_t __x;								\
		__x = (al) + (bl);							\
		(sh) = (ah) + (bh) + (__x < (al));			\
		(sl) = __x; 								\
	  } while (0)
#if MI_API_ENABLE
void telink_muladd(uECC_word_t a,
			uECC_word_t b,
			uECC_word_t *r0,
			uECC_word_t *r1,
			uECC_word_t *r2)
{
		  //	uECC_dword_t p = (uECC_dword_t)a * b;
		  uECC_dword_t p;
		  uECC_word_t *p0=(uECC_word_t *)&p, *p1=(uECC_word_t *)(p0+1);
#if MODULE_WATCHDOG_ENABLE
			  wd_clear();
#endif
		  gmp_umul_ppmm((*p1), (*p0), a, b);
		  
		  uECC_dword_t r01; 						  //  = ((uECC_dword_t)(*r1) << uECC_WORD_BITS) | *r0;
		  uECC_word_t *pr10=(uECC_word_t *)&r01, *pr11=(uECC_word_t *)(pr10+1);
		  *pr10 = *r0; *pr11 = *r1;
		  
		  gmp_add_ssaaaa((*pr11), (*pr10), (*pr11), (*pr10), (*p1), (*p0)); // r01 += p;
		  *r2 += (r01 < p);
		  *r1 = *pr11;
		  *r0 = *pr10;

}
#endif
void mz_mul2 (unsigned int * r, unsigned int * a, int na, unsigned int b);
void mpn_mul (unsigned int * r, unsigned int * a, int na, unsigned int * b, int nb){
	for(int k = 0; k < na + nb; k++){
		r[k] = 0;
	}
	u32 val = irq_disable();
	while(nb --){
		#if MODULE_WATCHDOG_ENABLE
		wd_clear();
		#endif
		mz_mul2(r++, a, na, *b++);
	}
	irq_restore(val);
}

#if (MI_SWITCH_LPN_EN||DU_LPN_EN)
typedef struct{
    u32 last_tick;
	u32 run_ms;
	u32 sleep_ms;
	u32 mode;
}mi_mesh_sleep_pre_t;
#define MI_MESH_RUN_MODE 	0
#define MI_MESH_SLEEP_MODE 	1
#if DU_LPN_EN
	#if LPN_CONTROL_EN
#define MI_RUN_INTERVAL		 3
#define MI_RUN_LISTEN		 25
#define MI_SLEEP_INTERVAL 	 (DU_ADV_INTER_MS-MI_RUN_INTERVAL)
#define MI_SLEEP_LITSEN_INTERVAL (DU_ADV_INTER_MS-MI_RUN_LISTEN)	
#define SOFT_TIMER_INTER_LPN 10
mi_mesh_sleep_pre_t	mi_mesh_sleep_time={0, MI_RUN_LISTEN,MI_SLEEP_LITSEN_INTERVAL,MI_MESH_RUN_MODE};
u8 save_power_mode =0;
u32 listen_cnt =0;

void set_du_lpn_mode(u8 en)
{
	save_power_mode = en;
}

	#else
#define MI_RUN_INTERVAL		 3
#define MI_SLEEP_INTERVAL 	 (DU_ADV_INTER_MS-MI_RUN_INTERVAL)
#define SOFT_TIMER_INTER_LPN 10
mi_mesh_sleep_pre_t	mi_mesh_sleep_time={0, MI_RUN_INTERVAL,MI_SLEEP_INTERVAL,MI_MESH_RUN_MODE};
	#endif
#else
#define MI_SLEEP_INTERVAL 	1140
#define MI_RUN_INTERVAL		60
#define SOFT_TIMER_INTER_LPN 50
mi_mesh_sleep_pre_t	mi_mesh_sleep_time={0, MI_RUN_INTERVAL,MI_SLEEP_INTERVAL,MI_MESH_RUN_MODE};
#endif


void mi_mesh_blt_send_adv2_scan_mode()
{
	#if MI_SWITCH_LPN_EN||DU_LPN_EN
	mesh_send_adv2scan_mode(1);
	#endif
}

int soft_timer_proc_mi_beacon(void)
{
	//gpio 0 toggle to see the effect
	DBG_CHN4_TOGGLE;
	static u32 mi_beacon_cnt ;
	mi_beacon_cnt++;
	mi_mesh_blt_send_adv2_scan_mode();
	if(mi_beacon_cnt == 2){
		mi_beacon_cnt =0;
		return -1;
	}else{
		return 0;
	}
}

void mi_mesh_sleep_init()
{
	#if LPN_CONTROL_EN
	// only in the adv mode will update the tick part .
	if(blt_state != BLS_LINK_STATE_CONN){
		mi_mesh_sleep_time.last_tick = clock_time()|1;
		mi_mesh_sleep_time.mode = MI_MESH_RUN_MODE;
	}
	#else
	mi_mesh_sleep_time.last_tick = clock_time()|1;
	mi_mesh_sleep_time.mode = MI_MESH_RUN_MODE;
	#endif
	
}

u8 mi_mesh_sleep_time_exceed_adv_iner()
{
	if(clock_time_exceed(mi_mesh_sleep_time.last_tick,(MI_RUN_INTERVAL+MI_SLEEP_INTERVAL)*1000)){//1.26s
		return 1;
	}else{
		return 0;
	}
}

static u32 soft_timer_allow_mesh =1;
int soft_timer_proc_mesh_cmd(void)
{
	//gpio 0 toggle to see the effect
	DBG_CHN4_TOGGLE;
	mi_mesh_blt_send_adv2_scan_mode();
	if(my_fifo_data_cnt_get(&mesh_adv_cmd_fifo) == 0){
		soft_timer_allow_mesh = 1;
		return -1;
	}else{
		soft_timer_allow_mesh = 0;
		return 0;
	}
}
u8 mi_busy_state =0;
void mi_mesh_state_set(u8 state)
{
	mi_busy_state = state;
	if(state == 0){
		// set back to normal state 
		mi_mesh_sleep_init();
		du_bls_ll_setAdvEnable(0);
	}else{
		du_bls_ll_setAdvEnable(1);
	}
}

u8 mi_mesh_get_state()
{
	return mi_busy_state;
}

	#if LPN_CONTROL_EN
void mesh_inter_cmd_proc(u8 en)
{
	if(en){
	// go to the interval of provision proc part 
		if(soft_timer_allow_mesh){
			if(blt_soft_timer_add((blt_timer_callback_t)&soft_timer_proc_mesh_cmd, SOFT_TIMER_INTER_LPN*1000)){
				soft_timer_allow_mesh = 0;
			}
		}
	}else{
		blt_soft_timer_delete((blt_timer_callback_t)&soft_timer_proc_mesh_cmd);
		soft_timer_allow_mesh = 1;
	}
}

void mi_mesh_lowpower_loop()
{
	if(du_ota_get_flag()){
		#if LPN_FAST_OTA_EN
		bls_pm_setSuspendMask (SUSPEND_DISABLE);
		#else
		// only for the mode of the single bus switch project, to save power.
		bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
		#endif
	}else if(mi_mesh_get_state()|| du_ota_reboot_flag){
		bls_pm_setSuspendMask (SUSPEND_DISABLE);
		mesh_inter_cmd_proc(mi_mesh_get_state()||du_ota_reboot_flag);
	}else if (blt_state == BLS_LINK_STATE_CONN){// in the connect mode , not the ota mode .
		// only receive the gatt-proxy mesh cmd .
		// mesh_inter_cmd_proc(my_fifo_data_cnt_get(&mesh_adv_cmd_fifo) > 0);
		bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	}else if (blt_state != BLS_LINK_STATE_CONN){
		if(save_power_mode){			
			if( clock_time_exceed(mi_mesh_sleep_time.last_tick,MI_RUN_INTERVAL *1000) &&
				mi_mesh_sleep_time.mode == MI_MESH_RUN_MODE){
				LOG_MSG_INFO(TL_LOG_NODE_SDK,0,0,"go to adv mode %x",clock_time()/16000);
				// send adv only for connect proc .
				mi_mesh_sleep_time.mode = MI_MESH_SLEEP_MODE;
				// in the power save ,still need to send packet.
				if(is_provision_success() && my_fifo_data_cnt_get(&mesh_adv_cmd_fifo) > 0){
					if(soft_timer_allow_mesh){
						if(blt_soft_timer_update(&soft_timer_proc_mesh_cmd, SOFT_TIMER_INTER_LPN*1000))
						{
							soft_timer_allow_mesh = 0;
						}
					}
				}
				bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
			}
		}else{
			// need to listen the adv packet from the gateway part 
			u32 run_time =0;
			if(listen_cnt%2==0){
				run_time = MI_RUN_LISTEN *1000;
			}else{
				run_time = MI_RUN_INTERVAL *1000;
			}
			if( clock_time_exceed(mi_mesh_sleep_time.last_tick,run_time) &&
				mi_mesh_sleep_time.mode == MI_MESH_RUN_MODE){
				LOG_MSG_INFO(TL_LOG_NODE_SDK,0,0,"go to listen mode %x",clock_time()/16000);
				mi_mesh_sleep_time.mode = MI_MESH_SLEEP_MODE;
				if(is_provision_success() && my_fifo_data_cnt_get(&mesh_adv_cmd_fifo) > 0){
					if(soft_timer_allow_mesh){
						if(blt_soft_timer_update(&soft_timer_proc_mesh_cmd, SOFT_TIMER_INTER_LPN*1000))
						{
							soft_timer_allow_mesh = 0;
						}
					}
				}
				listen_cnt++;
				bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
			}
		}
	}
}

	#else
void mi_mesh_lowpower_loop()
{
	if(blt_state == BLS_LINK_STATE_CONN || mi_mesh_get_state()||du_ota_reboot_flag){ // in the ble connection mode ,it will not trigger the deep mode ,and stop the mesh adv sending part
		bls_pm_setSuspendMask (SUSPEND_DISABLE);
		if(mi_mesh_get_state()||du_ota_reboot_flag){
			//LOG_MSG_INFO(TL_LOG_NODE_SDK,0,0,"run mode proc1",0);
			if(soft_timer_allow_mesh){
				if(blt_soft_timer_add((blt_timer_callback_t)&soft_timer_proc_mesh_cmd, SOFT_TIMER_INTER_LPN*1000))
				{
					soft_timer_allow_mesh = 0;
				}
			}
		}else{
			blt_soft_timer_delete((blt_timer_callback_t)&soft_timer_proc_mesh_cmd);
			soft_timer_allow_mesh = 1;
		}
	}else if (blt_state == BLS_LINK_STATE_ADV){
	
		if( clock_time_exceed(mi_mesh_sleep_time.last_tick,mi_mesh_sleep_time.run_ms *1000) &&
			mi_mesh_sleep_time.mode == MI_MESH_RUN_MODE){
			
			LOG_MSG_INFO(TL_LOG_NODE_SDK,0,0,"run mode proc0",0);
			mi_mesh_sleep_time.mode = MI_MESH_SLEEP_MODE;
			if(my_fifo_data_cnt_get(&mesh_adv_cmd_fifo) > 0){
				if(soft_timer_allow_mesh){
					if(blt_soft_timer_update(&soft_timer_proc_mesh_cmd, SOFT_TIMER_INTER_LPN*1000))
					{
						soft_timer_allow_mesh = 0;
					}
				}
			}
			bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
		}
	}
}
	#endif
#endif


