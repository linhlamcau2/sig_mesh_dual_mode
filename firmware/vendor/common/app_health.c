/********************************************************************************************************
 * @file	app_health.c
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
#include "proj_lib/ble/ll/ll.h"
#include "proj_lib/ble/blt_config.h"
#include "proj_lib/sig_mesh/app_mesh.h"
#include "proj_lib/mesh_crypto/mesh_crypto.h"
#include "proj_lib/pm.h"
#include "app_proxy.h"
#include "app_health.h"
#define ATTENTION_VAL_100_MS	10
#define HEALTH_INTERVAL_DEF_100MS	10

model_health_t 			model_sig_health;

u8 init_health_para()
{
	mesh_health_mag_sts_t *p_health = &(model_sig_health.srv.health_mag);
	p_health->cur_sts.company_id = p_health->fault_sts.company_id = g_vendor_id;
	p_health->period_sts.fast_period_log = 0;
	p_health->attention_timer = 0x00;
	p_health->dst_adr = 0x00;
	return 1;
}

int mesh_health_cur_sts_publish(u8 idx)
{
	int err =-1;
	model_common_t *p_com_md = &model_sig_health.srv.com;
	u16 ele_adr = p_com_md->ele_adr;
	u16 pub_adr = p_com_md->pub_adr;
	
	if(!pub_adr){
		return err;
	}

	u8 *uuid = 0;
	if(is_virtual_adr(pub_adr)){
		uuid = p_com_md->pub_uuid;
	}
	err = mesh_tx_cmd_rsp(HEALTH_CURRENT_STATUS,(u8 *)(&model_sig_health.srv.health_mag.cur_sts.test_id),OFFSETOF(mesh_health_current_sts_t,fault_array)+model_sig_health.srv.health_mag.cur_sts.cur_fault_idx,ele_adr,pub_adr,uuid,p_com_md);
	return err;
}

int mesh_health_cur_fault_add(u8 fault)
{
	int err = -1;
	if(NO_FAULT == fault){
		return err ;
	}

	mesh_health_current_sts_t *p_cur_st = &model_sig_health.srv.health_mag.cur_sts;
	foreach(i, p_cur_st->cur_fault_idx){
		if(p_cur_st->fault_array[i]){
			return 0;
		}
	}
	
	
	if(p_cur_st->cur_fault_idx < HEALTH_TEST_LEN){
		p_cur_st->fault_array[p_cur_st->cur_fault_idx++] = fault;
		mesh_health_fault_sts_t *p_fault_st = &model_sig_health.srv.health_mag.fault_sts;
		if(p_fault_st->cur_fault_idx < HEALTH_TEST_LEN){
			p_fault_st->fault_array[p_fault_st->cur_fault_idx++] = fault;
		}
	}
	return -1;	
}

int mesh_health_cur_fault_remove(u8 fault)
{	
	mesh_health_current_sts_t *p_fault_st = &model_sig_health.srv.health_mag.cur_sts;

	if((NO_FAULT == fault) || !p_fault_st->cur_fault_idx){
		return 0 ;
	}
	
	foreach(i, p_fault_st->cur_fault_idx){
		if(p_fault_st->fault_array[i] == fault){			
			memcpy(p_fault_st->fault_array+i, p_fault_st->fault_array+i+1, p_fault_st->cur_fault_idx-1-i);
			p_fault_st->cur_fault_idx--;
			break;
		}
	}
	return 0;
}

int mesh_cmd_sig_health_cur_sts(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err = 0;
	if(cb_par->model){
    }
	return err;

}
int mesh_cmd_sig_health_fault_sts(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err = 0;
	if(cb_par->model){
    }
	return err;
}

int mesh_cmd_sig_fault_get(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	model_health_common_t *p_model = (model_health_common_t *)cb_par->model;
	mesh_health_fault_sts_t *p_fault_sts_t;
	p_fault_sts_t = &(model_sig_health.srv.health_mag.fault_sts);
	err = mesh_tx_cmd_rsp(HEALTH_FAULT_STATUS,(u8 *)(p_fault_sts_t),OFFSETOF(mesh_health_fault_sts_t,fault_array)+model_sig_health.srv.health_mag.fault_sts.cur_fault_idx,p_model->com.ele_adr,cb_par->adr_src,0,0);
	return err;
}
int mesh_cmd_sig_fault_clr(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	model_health_common_t *p_model = (model_health_common_t *)cb_par->model;
	mesh_health_clear_rel_t *p_rcv;
	p_rcv =(mesh_health_clear_rel_t *)par;
	if(model_sig_health.srv.health_mag.fault_sts.company_id == p_rcv->company_id){
		//clear fault sts 
		memset(model_sig_health.srv.health_mag.fault_sts.fault_array,0,HEALTH_TEST_LEN);
		model_sig_health.srv.health_mag.fault_sts.cur_fault_idx = 0;
	}
	if(HEALTH_FAULT_CLEAR_NOACK != cb_par->op){
        err = mesh_tx_cmd_rsp(HEALTH_FAULT_STATUS,(u8 *)(&model_sig_health.srv.health_mag.fault_sts.test_id),OFFSETOF(mesh_health_fault_sts_t,fault_array),p_model->com.ele_adr,cb_par->adr_src,0,0);
    }else{
        err = 0;
    }
	
	return err;
}
int mesh_cmd_sig_fault_clr_unrel(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	err = mesh_cmd_sig_fault_clr(par,par_len,cb_par);
	return err;
}

u8 is_support_test_id(u8 id)
{
	for(u8 i=0; i<HEALTH_TEST_LEN; i++){
		if(model_sig_health.srv.health_mag.test_id_list[i] == id){
			return 1;
		}
	}
	return 0;
}

int mesh_cmd_sig_fault_test(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	model_health_common_t *p_model = (model_health_common_t *)cb_par->model;
	mesh_health_fault_test_t *p_fault_test ;
	p_fault_test = (mesh_health_fault_test_t *)par;
	//compare the test id and the company id 
	if((HEALTH_FAULT_TEST == cb_par->op)&&(p_fault_test->company_id == model_sig_health.srv.health_mag.fault_sts.company_id) && is_support_test_id(p_fault_test->test_id)){
		//test ; and return the sts

		err = mesh_tx_cmd_rsp(HEALTH_FAULT_STATUS,(u8 *)(&model_sig_health.srv.health_mag.fault_sts.test_id),OFFSETOF(mesh_health_fault_sts_t,fault_array)+model_sig_health.srv.health_mag.fault_sts.cur_fault_idx,p_model->com.ele_adr,cb_par->adr_src,0,0);
	}
	return err;
}
int mesh_cmd_sig_fault_test_unrel(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	err = mesh_cmd_sig_fault_test(par,par_len,cb_par);
	return err;
}

int mesh_cmd_sig_period_get(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	model_health_common_t *p_model = (model_health_common_t *)cb_par->model;
	err = mesh_tx_cmd_rsp(HEALTH_PERIOD_STATUS,(u8 *)(&model_sig_health.srv.health_mag.period_sts.fast_period_log),sizeof(mesh_health_period_status_t),p_model->com.ele_adr,cb_par->adr_src,0,0);
	return err;
}

int mesh_cmd_sig_period_set(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	model_health_common_t *p_model = (model_health_common_t *)cb_par->model;
	model_sig_health.srv.health_mag.period_sts.fast_period_log = par[0];
	if(HEALTH_PERIOD_SET_NOACK != cb_par->op){
		err = mesh_tx_cmd_rsp(HEALTH_PERIOD_STATUS,(u8 *)(&model_sig_health.srv.health_mag.period_sts.fast_period_log),sizeof(mesh_health_period_status_t),p_model->com.ele_adr,cb_par->adr_src,0,0);
	}
	else{
		err = 0;
	}
	return err;
}
int mesh_cmd_sig_period_set_unrel(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	err = mesh_cmd_sig_period_set(par, par_len, cb_par);
	return err;
}
int mesh_cmd_sig_period_status(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	if(cb_par->model!=0){
		err=0;
	}
	return err;
}

int mesh_cmd_sig_attention_get(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
	model_health_common_t *p_model = (model_health_common_t *)cb_par->model;
	err = mesh_tx_cmd_rsp(HEALTH_ATTENTION_STATUS,(u8 *)(&model_sig_health.srv.health_mag.attention_timer),1,p_model->com.ele_adr,cb_par->adr_src,0,0);
	return err;
}
int mesh_cmd_sig_attention_set(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err =-1;
#if PTS_TEST_EN
	if(par_len > 1){ // parameter length invalid		
		err = OP_AGG_UNKNOWN_MSG;//in op agg pts case, need report Message parameters not understood by the identified model
		return err;
	}
#endif

	model_health_common_t *p_model = (model_health_common_t *)cb_par->model;
	model_sig_health.srv.health_mag.attention_timer = par[0];
	if(HEALTH_ATTENTION_SET_NOACK != cb_par->op){
		err = mesh_tx_cmd_rsp(HEALTH_ATTENTION_STATUS,(u8 *)(&model_sig_health.srv.health_mag.attention_timer),1,p_model->com.ele_adr,cb_par->adr_src,0,0);
	}
	else{
		err = 0;
	}
	return err;
}
int mesh_cmd_sig_attention_status(u8 *par, int par_len, mesh_cb_fun_par_t *cb_par)
{
	int err = 0;

	return err;
}









