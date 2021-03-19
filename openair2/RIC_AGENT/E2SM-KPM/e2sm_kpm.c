/*
 * SPDX-FileCopyrightText: 2020-present Open Networking Foundation <info@opennetworking.org>
 *
 * SPDX-License-Identifier: LicenseRef-ONF-Member-1.0
 */

/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include "common/utils/assertions.h"
#include "f1ap_common.h"
#include "ric_agent.h"
#include "e2ap_encoder.h"
#include "e2sm_kpm.h"

#include "E2AP_Cause.h"
#include "E2SM_KPM_E2SM-KPM-RANfunction-Description.h"
#include "E2SM_KPM_RIC-KPMNode-Item.h"
#include "E2SM_KPM_Cell-Measurement-Object-Item.h"
#include "E2SM_KPM_RIC-EventTriggerStyle-Item.h"
#include "E2SM_KPM_RIC-ReportStyle-Item.h"
#include "E2SM_KPM_MeasurementInfo-Action-Item.h"
#include "E2SM_KPM_E2SM-KPM-ActionDefinition-Format1.h"
#include "E2SM_KPM_MeasurementInfoItem.h"
#include "E2SM_KPM_E2SM-KPM-ActionDefinition.h"
#include "E2SM_KPM_MeasurementRecord.h"
#include "E2SM_KPM_MeasurementRecordItem.h"
//#include "E2SM_KPM_RIC-ReportStyle-List.h"
//#include "E2SM_KPM_RIC-EventTriggerStyle-List.h"
#include "E2SM_KPM_E2SM-KPM-IndicationMessage.h"
//#include "E2SM_KPM_OCUCP-PF-Container.h"
//#include "E2SM_KPM_PF-Container.h"
//#include "E2SM_KPM_PM-Containers-List.h"
#include "E2AP_ProtocolIE-Field.h"
#include "E2SM_KPM_E2SM-KPM-IndicationHeader.h"
#include "E2SM_KPM_SNSSAI.h"
#include "E2SM_KPM_GlobalKPMnode-ID.h"
#include "E2SM_KPM_GNB-ID-Choice.h"
#include "E2SM_KPM_NRCGI.h"

extern f1ap_cudu_inst_t f1ap_cu_inst[MAX_eNB];
extern int global_e2_node_id(ranid_t ranid, E2AP_GlobalE2node_ID_t* node_id);
extern RAN_CONTEXT_t RC;

/**
 ** The main thing with this abstraction is that we need per-SM modules
 ** to handle the details of the function, event trigger, action, etc
 ** definitions... and to actually do all the logic and implement the
 ** inner parts of the message encoding.  generic e2ap handles the rest.
 **/

static int e2sm_kpm_subscription_add(ric_agent_info_t *ric, ric_subscription_t *sub);
static int e2sm_kpm_subscription_del(ric_agent_info_t *ric, ric_subscription_t *sub, int force,long *cause,long *cause_detail);
static int e2sm_kpm_control(ric_agent_info_t *ric,ric_control_t *control);
static int e2sm_kpm_timer_expiry(
        ric_agent_info_t *ric,
        long timer_id,
        ric_ran_function_id_t function_id,
        long request_id,
        long instance_id,
        long action_id,
        uint8_t **outbuf,
        uint32_t *outlen);
static E2SM_KPM_E2SM_KPM_IndicationMessage_t* encode_kpm_Indication_Msg(ric_agent_info_t* ric, ric_subscription_t *rs);
static void generate_e2apv1_indication_request_parameterized(E2AP_E2AP_PDU_t *e2ap_pdu, long requestorId, long instanceId, long ranFunctionId, long actionId, long seqNum, uint8_t *ind_header_buf, int header_length, uint8_t *ind_message_buf, int message_length);
static void encode_e2sm_kpm_indication_header(ranid_t ranid, E2SM_KPM_E2SM_KPM_IndicationHeader_t *ihead);

static int e2ap_asn1c_encode_pdu(E2AP_E2AP_PDU_t* pdu, unsigned char **buffer);

#define MAX_KPM_MEAS    5

kmp_meas_info_t e2sm_kpm_meas_info[MAX_KPM_MEAS] = {
                                            {1, "RRC.ConnEstabAtt.sum", 15, FALSE},
                                            {2, "RRC.ConnEstabSucc.sum", 20, FALSE},
                                            {3, "RRC.ConnReEstabAtt.sum", 25, FALSE},
                                            {4, "RRC.ConnMean", 30, FALSE},
                                            {5, "RRC.ConnMax", 35, FALSE}
                                        };

static ric_service_model_t e2sm_kpm_model = {
    .name = "e2sm_kpm-v2beta1",
    .oid = "1.3.6.1.4.1.1.1.2.2",
    .handle_subscription_add = e2sm_kpm_subscription_add,
    .handle_subscription_del = e2sm_kpm_subscription_del,
    .handle_control = e2sm_kpm_control,
    .handle_timer_expiry = e2sm_kpm_timer_expiry
};

/**
 * Initializes KPM state and registers KPM e2ap_ran_function_id_t number(s).
 */
int e2sm_kpm_init(void)
{
    uint16_t i;
    ric_ran_function_t *func;
    E2SM_KPM_E2SM_KPM_RANfunction_Description_t *func_def;
    E2SM_KPM_RIC_ReportStyle_Item_t *ric_report_style_item;
    E2SM_KPM_RIC_EventTriggerStyle_Item_t *ric_event_trigger_style_item;
    E2SM_KPM_RIC_KPMNode_Item_t *ric_kpm_node_item;
    E2SM_KPM_Cell_Measurement_Object_Item_t *cell_meas_object_item;
    E2SM_KPM_MeasurementInfo_Action_Item_t *meas_action_item1;
    E2SM_KPM_MeasurementInfo_Action_Item_t *meas_action_item2;
    E2SM_KPM_MeasurementInfo_Action_Item_t *meas_action_item3;
    E2SM_KPM_MeasurementInfo_Action_Item_t *meas_action_item4;
    E2SM_KPM_MeasurementInfo_Action_Item_t *meas_action_item5;

    func = (ric_ran_function_t *)calloc(1, sizeof(*func));
    func->model = &e2sm_kpm_model;
    func->revision = 1;
    func->name = "ORAN-E2SM-KPM";
    func->description = "KPM monitor";


    func_def = (E2SM_KPM_E2SM_KPM_RANfunction_Description_t *)calloc(1, sizeof(*func_def));

    /* RAN Function Name */
    func_def->ranFunction_Name.ranFunction_ShortName.buf = (uint8_t *)strdup(func->name);
    func_def->ranFunction_Name.ranFunction_ShortName.size = strlen(func->name);
    func_def->ranFunction_Name.ranFunction_E2SM_OID.buf = (uint8_t *)strdup(func->model->oid);
    func_def->ranFunction_Name.ranFunction_E2SM_OID.size = strlen(func->model->oid);
    func_def->ranFunction_Name.ranFunction_Description.buf = (uint8_t *)strdup(func->description);
    func_def->ranFunction_Name.ranFunction_Description.size = strlen(func->description);

    /* KPM Node List */
    func_def->ric_KPM_Node_List = (struct E2SM_KPM_E2SM_KPM_RANfunction_Description__ric_KPM_Node_List *)calloc(1, sizeof(*func_def->ric_KPM_Node_List));
    ric_kpm_node_item = (E2SM_KPM_RIC_KPMNode_Item_t *)calloc(1, sizeof(*ric_kpm_node_item));
    ric_kpm_node_item->ric_KPMNode_Type.present = E2SM_KPM_GlobalKPMnode_ID_PR_eNB; 
    
    /* Fetching PLMN ID*/
    for (i = 0; i < RC.nb_inst; ++i) { //is there a better way to fetch RANID,otherwise PLMNID of first intance will get populated ?
        if ( (e2_conf[i]->enabled) && 
             ((e2_conf[i]->e2node_type == E2NODE_TYPE_ENB_CU) || (e2_conf[i]->e2node_type == E2NODE_TYPE_NG_ENB_CU)) 
           ){
            MCC_MNC_TO_PLMNID(
                e2_conf[i]->mcc,
                e2_conf[i]->mnc,
                e2_conf[i]->mnc_digit_length,
                &ric_kpm_node_item->ric_KPMNode_Type.choice.eNB.global_eNB_ID.pLMN_Identity);
            break;
        }
    }

    //ric_kpm_node_item->ric_KPMNode_Type.choice.eNB.global_eNB_ID.pLMN_Identity.buf = (uint8_t *)strdup("PLMNID");//pending
    //ric_kpm_node_item->ric_KPMNode_Type.choice.eNB.global_eNB_ID.pLMN_Identity.size = strlen("PLMNID");   //pending
    ric_kpm_node_item->cell_Measurement_Object_List = 
            (struct E2SM_KPM_RIC_KPMNode_Item__cell_Measurement_Object_List *)calloc(1, sizeof(*ric_kpm_node_item->cell_Measurement_Object_List));
    
    cell_meas_object_item = (E2SM_KPM_Cell_Measurement_Object_Item_t *)calloc(1, sizeof(*cell_meas_object_item));
    cell_meas_object_item->cell_object_ID.buf = (uint8_t *)strdup("EUtranCellFDD"); //if cell is TDD then EUtranCellTDD 
    cell_meas_object_item->cell_object_ID.size = strlen("EUtranCellFDD");
    cell_meas_object_item->cell_global_ID.present = E2SM_KPM_CellGlobalID_PR_eUTRA_CGI;

    MCC_MNC_TO_PLMNID(e2_conf[i]->mcc,
                      e2_conf[i]->mnc,
                      e2_conf[i]->mnc_digit_length,
                      &cell_meas_object_item->cell_global_ID.choice.eUTRA_CGI.pLMN_Identity);
 
    //cell_meas_object_item->cell_global_ID.choice.eUTRA_CGI.pLMN_Identity.buf = (uint8_t *)strdup("PLMNID"); //pending
    //cell_meas_object_item->cell_global_ID.choice.eUTRA_CGI.pLMN_Identity.size = strlen("PLMNID"); //pending
    
    MACRO_ENB_ID_TO_BIT_STRING(e2_conf[i]->cell_identity,
                               &cell_meas_object_item->cell_global_ID.choice.eUTRA_CGI.eUTRACellIdentity);

    //cell_meas_object_item->cell_global_ID.choice.eUTRA_CGI.eUTRACellIdentity.buf = (uint8_t *)strdup("");//pending
    //cell_meas_object_item->cell_global_ID.choice.eUTRA_CGI.eUTRACellIdentity.size = strlen("");//pending
    //cell_meas_object_item->cell_global_ID.choice.eUTRA_CGI.eUTRACellIdentity.bits_unused = 0;//pending
    ASN_SEQUENCE_ADD(&ric_kpm_node_item->cell_Measurement_Object_List->list, cell_meas_object_item);

    ASN_SEQUENCE_ADD(&func_def->ric_KPM_Node_List->list, ric_kpm_node_item);

    /* Sequence of Event trigger styles */
    func_def->ric_EventTriggerStyle_List = (struct E2SM_KPM_E2SM_KPM_RANfunction_Description__ric_EventTriggerStyle_List *)calloc(1, sizeof(*func_def->ric_EventTriggerStyle_List));
    ric_event_trigger_style_item = (E2SM_KPM_RIC_EventTriggerStyle_Item_t *)calloc(1, sizeof(*ric_event_trigger_style_item));
    ric_event_trigger_style_item->ric_EventTriggerStyle_Type = 1;
    ric_event_trigger_style_item->ric_EventTriggerStyle_Name.buf = (uint8_t *)strdup("Trigger1");
    ric_event_trigger_style_item->ric_EventTriggerStyle_Name.size = strlen("Trigger1");
    ric_event_trigger_style_item->ric_EventTriggerFormat_Type = 1;
    ASN_SEQUENCE_ADD(&func_def->ric_EventTriggerStyle_List->list, ric_event_trigger_style_item);

    /* Sequence of Report styles */
    func_def->ric_ReportStyle_List = (struct E2SM_KPM_E2SM_KPM_RANfunction_Description__ric_ReportStyle_List *)calloc(1, sizeof(*func_def->ric_ReportStyle_List));
    ric_report_style_item = (E2SM_KPM_RIC_ReportStyle_Item_t *)calloc(1, sizeof(*ric_report_style_item));
    ric_report_style_item->ric_ReportStyle_Type = 6;
    ric_report_style_item->ric_ReportStyle_Name.buf = (uint8_t *)strdup("O-CU-UP Measurement Container for the EPC connected deployment");
    ric_report_style_item->ric_ReportStyle_Name.size = strlen("O-CU-UP Measurement Container for the EPC connected deployment");
    ric_report_style_item->ric_ActionFormat_Type = 6; //pending 
    
    meas_action_item1 = (E2SM_KPM_MeasurementInfo_Action_Item_t *)calloc(1, sizeof(*meas_action_item1));
    meas_action_item1->measName.buf = (uint8_t *)strdup(e2sm_kpm_meas_info[0].meas_type_name);
    meas_action_item1->measName.size = strlen(e2sm_kpm_meas_info[0].meas_type_name);
    meas_action_item1->measID = (E2SM_KPM_MeasurementTypeID_t *)&e2sm_kpm_meas_info[0].meas_type_id;
    ASN_SEQUENCE_ADD(&ric_report_style_item->measInfo_Action_List.list, meas_action_item1);
    
    meas_action_item2 = (E2SM_KPM_MeasurementInfo_Action_Item_t *)calloc(1, sizeof(*meas_action_item2));
    meas_action_item2->measName.buf = (uint8_t *)strdup(e2sm_kpm_meas_info[1].meas_type_name); //(uint8_t *)strdup("RRC.ConnEstabSucc.sum");
    meas_action_item2->measName.size = strlen(e2sm_kpm_meas_info[1].meas_type_name);
    meas_action_item2->measID = (E2SM_KPM_MeasurementTypeID_t *)&e2sm_kpm_meas_info[1].meas_type_id;
    ASN_SEQUENCE_ADD(&ric_report_style_item->measInfo_Action_List.list, meas_action_item2);
    
    meas_action_item3 = (E2SM_KPM_MeasurementInfo_Action_Item_t *)calloc(1, sizeof(*meas_action_item3));
    meas_action_item3->measName.buf = (uint8_t *)strdup(e2sm_kpm_meas_info[2].meas_type_name);
    meas_action_item3->measName.size = strlen(e2sm_kpm_meas_info[2].meas_type_name);
    meas_action_item3->measID = (E2SM_KPM_MeasurementTypeID_t *)&e2sm_kpm_meas_info[2].meas_type_id;
    ASN_SEQUENCE_ADD(&ric_report_style_item->measInfo_Action_List.list, meas_action_item3);

    meas_action_item4 = (E2SM_KPM_MeasurementInfo_Action_Item_t *)calloc(1, sizeof(*meas_action_item4));
    meas_action_item4->measName.buf = (uint8_t *)strdup(e2sm_kpm_meas_info[3].meas_type_name);
    meas_action_item4->measName.size = strlen(e2sm_kpm_meas_info[3].meas_type_name);
    meas_action_item4->measID = (E2SM_KPM_MeasurementTypeID_t *)&e2sm_kpm_meas_info[3].meas_type_id;
    ASN_SEQUENCE_ADD(&ric_report_style_item->measInfo_Action_List.list, meas_action_item4);

    meas_action_item5 = (E2SM_KPM_MeasurementInfo_Action_Item_t *)calloc(1, sizeof(*meas_action_item5));
    meas_action_item5->measName.buf = (uint8_t *)strdup(e2sm_kpm_meas_info[4].meas_type_name);
    meas_action_item5->measName.size = strlen(e2sm_kpm_meas_info[4].meas_type_name);
    meas_action_item5->measID = (E2SM_KPM_MeasurementTypeID_t *)&e2sm_kpm_meas_info[4].meas_type_id;
    ASN_SEQUENCE_ADD(&ric_report_style_item->measInfo_Action_List.list, meas_action_item5);

    ric_report_style_item->ric_IndicationHeaderFormat_Type = 1;
    ric_report_style_item->ric_IndicationMessageFormat_Type = 1;
    ASN_SEQUENCE_ADD(&func_def->ric_ReportStyle_List->list, ric_report_style_item);

#if 0
    func_def->e2SM_KPM_RANfunction_Item.ric_EventTriggerStyle_List = \
    (struct E2SM_KPM_E2SM_KPM_RANfunction_Description__e2SM_KPM_RANfunction_Item__ric_EventTriggerStyle_List *)calloc(1, sizeof(*func_def->e2SM_KPM_RANfunction_Item.ric_EventTriggerStyle_List));
    ric_event_trigger_style_item = (E2SM_KPM_RIC_EventTriggerStyle_List_t *)calloc(1, sizeof(*ric_event_trigger_style_item));
    ric_event_trigger_style_item->ric_EventTriggerStyle_Type = 1;
    ric_event_trigger_style_item->ric_EventTriggerStyle_Name.buf = (uint8_t *)strdup("Trigger1");
    ric_event_trigger_style_item->ric_EventTriggerStyle_Name.size = strlen("Trigger1");
    ric_event_trigger_style_item->ric_EventTriggerFormat_Type = 1;
    ASN_SEQUENCE_ADD(&func_def->e2SM_KPM_RANfunction_Item.ric_EventTriggerStyle_List->list, ric_event_trigger_style_item);

    func_def->e2SM_KPM_RANfunction_Item.ric_ReportStyle_List = (struct E2SM_KPM_E2SM_KPM_RANfunction_Description__e2SM_KPM_RANfunction_Item__ric_ReportStyle_List *)calloc(1, sizeof(*func_def->e2SM_KPM_RANfunction_Item.ric_ReportStyle_List));

    ric_report_style_item = (E2SM_KPM_RIC_ReportStyle_List_t *)calloc(1, sizeof(*ric_report_style_item));
    ric_report_style_item->ric_ReportStyle_Type = 6;
    ric_report_style_item->ric_ReportStyle_Name.buf = (uint8_t *)strdup("O-CU-UP Measurement Container for the EPC connected deployment");
    ric_report_style_item->ric_ReportStyle_Name.size = strlen("O-CU-UP Measurement Container for the EPC connected deployment");
    ric_report_style_item->ric_IndicationHeaderFormat_Type = 1;
    ric_report_style_item->ric_IndicationMessageFormat_Type = 1;
    ASN_SEQUENCE_ADD(&func_def->e2SM_KPM_RANfunction_Item.ric_ReportStyle_List->list, ric_report_style_item);
#endif

    func->enc_definition_len = e2ap_encode(&asn_DEF_E2SM_KPM_E2SM_KPM_RANfunction_Description,0, func_def,&func->enc_definition);

    if (func->enc_definition_len < 0) {
        RIC_AGENT_ERROR("failed to encode RANfunction_List in E2SM KPM func description; aborting!");
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_E2SM_KPM_E2SM_KPM_RANfunction_Description, func_def);
        free(func_def);
        free(func);
        return -1;
    }

    func->enabled = 1;
    func->definition = func_def;

    return ric_agent_register_ran_function(func);
}

static int e2sm_kpm_subscription_add(ric_agent_info_t *ric, ric_subscription_t *sub)
{
  /* XXX: process E2SM content. */
  if (LIST_EMPTY(&ric->subscription_list)) {
    LIST_INSERT_HEAD(&ric->subscription_list,sub,subscriptions);
  }
  else {
    LIST_INSERT_BEFORE(LIST_FIRST(&ric->subscription_list),sub,subscriptions);
  }
  return 0;
}

static int e2sm_kpm_subscription_del(ric_agent_info_t *ric, ric_subscription_t *sub, int force,long *cause,long *cause_detail)
{
    timer_remove(ric->e2sm_kpm_timer_id);
    LIST_REMOVE(sub, subscriptions);
    ric_free_subscription(sub);
    return 0;
}

static int e2sm_kpm_control(ric_agent_info_t *ric,ric_control_t *control)
{
    return 0;
}

static int e2sm_kpm_timer_expiry(
        ric_agent_info_t *ric,
        long timer_id,
        ric_ran_function_id_t function_id,
        long request_id,
        long instance_id,
        long action_id,
        uint8_t **outbuf,
        uint32_t *outlen)
{

    E2SM_KPM_E2SM_KPM_IndicationMessage_t* indicationmessage;
    ric_subscription_t *rs;

    DevAssert(timer_id == ric->e2sm_kpm_timer_id);

    RIC_AGENT_INFO("Timer expired, timer_id %ld function_id %ld\n", timer_id, function_id);

    /* Fetch the RIC Subscription */
    rs = ric_agent_lookup_subscription(ric,request_id,instance_id,function_id);
    if (!rs) {
        RIC_AGENT_ERROR("failed to find subscription %ld/%ld/%ld\n", request_id,instance_id,function_id);
    }

    indicationmessage = encode_kpm_Indication_Msg(ric, rs);

    {
        char *error_buf = (char*)calloc(300, sizeof(char));
        size_t errlen;
        asn_check_constraints(&asn_DEF_E2SM_KPM_E2SM_KPM_IndicationMessage, indicationmessage, error_buf, &errlen);
        printf("error length %zu\n", errlen);
        printf("error buf %s\n", error_buf);
    xer_fprint(stderr, &asn_DEF_E2SM_KPM_E2SM_KPM_IndicationMessage, indicationmessage);
    }

    uint8_t e2smbuffer[8192];
    size_t e2smbuffer_size = 8192;

    asn_enc_rval_t er = asn_encode_to_buffer(NULL,
            ATS_ALIGNED_BASIC_PER,
            &asn_DEF_E2SM_KPM_E2SM_KPM_IndicationMessage,
            indicationmessage, e2smbuffer, e2smbuffer_size);

    fprintf(stderr, "er encded is %zu\n", er.encoded);
    fprintf(stderr, "after encoding message\n");

    E2AP_E2AP_PDU_t *e2ap_pdu = (E2AP_E2AP_PDU_t*)calloc(1, sizeof(E2AP_E2AP_PDU_t));

    E2SM_KPM_E2SM_KPM_IndicationHeader_t* ind_header_style1 =
        (E2SM_KPM_E2SM_KPM_IndicationHeader_t*)calloc(1,sizeof(E2SM_KPM_E2SM_KPM_IndicationHeader_t));

    encode_e2sm_kpm_indication_header(ric->ranid, ind_header_style1);

    uint8_t e2sm_header_buf_style1[8192];
    size_t e2sm_header_buf_size_style1 = 8192;
    asn_enc_rval_t er_header_style1 = asn_encode_to_buffer(
            NULL,
            ATS_ALIGNED_BASIC_PER,
            &asn_DEF_E2SM_KPM_E2SM_KPM_IndicationHeader,
            ind_header_style1,
            e2sm_header_buf_style1,
            e2sm_header_buf_size_style1);

    if (er_header_style1.encoded < 0) {
        fprintf(stderr, "ERROR encoding indication header, name=%s, tag=%s", er_header_style1.failed_type->name, er_header_style1.failed_type->xml_tag);
    }

    DevAssert(er_header_style1.encoded >= 0);

    // TODO - remove hardcoded values
    generate_e2apv1_indication_request_parameterized(
            e2ap_pdu, request_id, instance_id, function_id, action_id,
            0, e2sm_header_buf_style1, er_header_style1.encoded,
            e2smbuffer, er.encoded);

    *outlen = e2ap_asn1c_encode_pdu(e2ap_pdu, outbuf);

    return 0;
}

static E2SM_KPM_E2SM_KPM_IndicationMessage_t*
encode_kpm_Indication_Msg(ric_agent_info_t* ric, ric_subscription_t *rs)
{
    int ret;
    ric_action_t *action;
    E2SM_KPM_E2SM_KPM_ActionDefinition_t *actionDef;
    asn_dec_rval_t decode_result;
    E2SM_KPM_SubscriptionID_t subscriptionID;
    E2SM_KPM_E2SM_KPM_ActionDefinition_Format1_t *actionDefFormat1;
    E2SM_KPM_MeasurementRecordItem_t *indMsgMeasRecItemArr[MAX_KPM_MEAS];
    E2SM_KPM_MeasurementInfoItem_t *indMsgMeasInfoItemArr[MAX_KPM_MEAS];
    E2SM_KPM_MeasurementInfoItem_t *actionDefMeasInfoItem;
    uint8_t indMsgMeasInfoCnt = 0;
    uint8_t i;

    /*Reset Subscriptions */
    for (i = 0; i < MAX_KPM_MEAS; i++)
    {   
        e2sm_kpm_meas_info[i].subscription_status = FALSE;
    }

    /* Fetch the RIC Action from action list of  RIC Subscription */
    LIST_FOREACH(action, &rs->action_list, actions) 
    { /*Expecting just 1 RIC action, as all Measurement Info will be inside def_buff */
        if (!action->enabled) {
            continue;
        }

        if (action->def_size > 0)
        {
            /* ASN Decode the Action Definition */
            decode_result = aper_decode_complete(NULL, &asn_DEF_E2SM_KPM_E2SM_KPM_ActionDefinition, 
                                                (void **)&actionDef, action->def_buf, action->def_size);
            DevAssert(decode_result.code == RC_OK);
            xer_fprint(stdout, &asn_DEF_E2SM_KPM_E2SM_KPM_ActionDefinition, actionDef);
            
            if (actionDef->actionDefinition_formats.present ==
                                        E2SM_KPM_E2SM_KPM_ActionDefinition__actionDefinition_formats_PR_actionDefinition_Format1)
            {
                actionDefFormat1 = &actionDef->actionDefinition_formats.choice.actionDefinition_Format1;
                subscriptionID = actionDefFormat1->subscriptID;
                
                /* Fetch Measurement IDs */
                for (i=0; i < actionDefFormat1->measInfoList.list.count; i++)
                {
                    actionDefMeasInfoItem = (E2SM_KPM_MeasurementInfoItem_t *)(actionDefFormat1->measInfoList.list.array[i]);
                    
                    if (actionDefMeasInfoItem->measType.present == E2SM_KPM_MeasurementType_PR_measID)
                    {
                        if ( (actionDefMeasInfoItem->measType.choice.measID < (MAX_KPM_MEAS+1)) &&  /*Expecting KPM MeasID to be within limits */
                             (e2sm_kpm_meas_info[actionDefMeasInfoItem->measType.choice.measID-1].subscription_status == FALSE) ) /*Avoid subscribing duplicate */
                        {
                            indMsgMeasInfoItemArr[indMsgMeasInfoCnt] = 
                                                (E2SM_KPM_MeasurementInfoItem_t *)calloc(1,sizeof(E2SM_KPM_MeasurementInfoItem_t));
                            indMsgMeasInfoItemArr[indMsgMeasInfoCnt]->measType.present = E2SM_KPM_MeasurementType_PR_measID;
                            indMsgMeasInfoItemArr[indMsgMeasInfoCnt]->measType.choice.measID = actionDefMeasInfoItem->measType.choice.measID;
                            e2sm_kpm_meas_info[actionDefMeasInfoItem->measType.choice.measID-1].subscription_status = TRUE;
                            indMsgMeasInfoCnt++;
                        }
                    }
                }
            }
        }   
    }

    for (i=0; i < indMsgMeasInfoCnt; i++)
    {
        indMsgMeasRecItemArr[i] = (E2SM_KPM_MeasurementRecordItem_t *)calloc(1,sizeof(E2SM_KPM_MeasurementRecordItem_t));
        switch(indMsgMeasInfoItemArr[i]->measType.choice.measID)
        {
            case 1:
            case 2:
            case 3:
            case 5:
                indMsgMeasRecItemArr[i]->present = E2SM_KPM_MeasurementRecordItem_PR_integer;
                indMsgMeasRecItemArr[i]->choice.integer = /* For now stubbing the values untill actual values are fetched from RRC */
                            e2sm_kpm_meas_info[indMsgMeasInfoItemArr[i]->measType.choice.measID-1].meas_data;
            case 4:
                indMsgMeasRecItemArr[i]->present = E2SM_KPM_MeasurementRecordItem_PR_integer;
                indMsgMeasRecItemArr[i]->choice.integer = f1ap_cu_inst[ric->ranid].num_ues;
            default:
                break;
        }
    } 
#if 0
    /*
     * OCUCP_PF_Container
     */
    E2SM_KPM_OCUCP_PF_Container_t* cucpcont = (E2SM_KPM_OCUCP_PF_Container_t*)calloc(1, sizeof(E2SM_KPM_OCUCP_PF_Container_t));
    ASN_STRUCT_RESET(asn_DEF_E2SM_KPM_OCUCP_PF_Container, cucpcont);

    {
        char *node_name = strdup(e2_conf[ric->ranid]->node_name);
        cucpcont->gNB_CU_CP_Name = (E2SM_KPM_GNB_CU_CP_Name_t*)calloc(1, sizeof(E2SM_KPM_GNB_CU_CP_Name_t));
        cucpcont->gNB_CU_CP_Name->buf = (uint8_t*)calloc(cucpcont->gNB_CU_CP_Name->size, sizeof(char));
        cucpcont->gNB_CU_CP_Name->size = strlen(node_name) + 1;
        strncpy((char*)cucpcont->gNB_CU_CP_Name->buf, node_name, cucpcont->gNB_CU_CP_Name->size);
    }

    cucpcont->cu_CP_Resource_Status.numberOfActive_UEs = (long*)calloc(1, sizeof(long));
    *cucpcont->cu_CP_Resource_Status.numberOfActive_UEs = f1ap_cu_inst[ric->ranid].num_ues;

    /*
     * PF_Container -> OCUCP_PF_Container
     */
    E2SM_KPM_PF_Container_t* pfcontainer = (E2SM_KPM_PF_Container_t*)calloc(1, sizeof(E2SM_KPM_PF_Container_t));
    pfcontainer->present = E2SM_KPM_PF_Container_PR_oCU_CP;
    pfcontainer->choice.oCU_CP = *cucpcont;

    /*
     * Containers_List -> PF_Container
     */
    E2SM_KPM_PM_Containers_List_t* containers_list = (E2SM_KPM_PM_Containers_List_t*)calloc(1, sizeof(E2SM_KPM_PM_Containers_List_t));
    ASN_STRUCT_RESET(asn_DEF_E2SM_KPM_PM_Containers_List, containers_list);
    containers_list->performanceContainer = pfcontainer;
#endif

   /*
    * Measurement Record->MeasurementRecordItem (List)
    */
    E2SM_KPM_MeasurementRecord_t* meas_rec = (E2SM_KPM_MeasurementRecord_t *)calloc(1, sizeof(E2SM_KPM_MeasurementRecord_t));
    for(i=0; i < indMsgMeasInfoCnt; i++)
    { 
        ret = ASN_SEQUENCE_ADD(&meas_rec->list, &indMsgMeasRecItemArr[i]);
        DevAssert(ret == 0);
    }

   /*
    * measData->measurementRecord (List)
    */
    E2SM_KPM_MeasurementData_t* meas_data = (E2SM_KPM_MeasurementData_t*)calloc(1, sizeof(E2SM_KPM_MeasurementData_t));
    ret = ASN_SEQUENCE_ADD(&meas_data->list, meas_rec);
    DevAssert(ret == 0);

   /*
    * measInfoList
    */
    E2SM_KPM_MeasurementInfoList_t* meas_info_list = (E2SM_KPM_MeasurementInfoList_t*)calloc(1, sizeof(E2SM_KPM_MeasurementInfoList_t));
    for(i=0; i < indMsgMeasInfoCnt; i++)
    {
        ret = ASN_SEQUENCE_ADD(&meas_info_list->list,&indMsgMeasInfoItemArr[i]);
        DevAssert(ret == 0);
    }

    /*
     * IndicationMessage_Format1 -> measInfoList
     * IndicationMessage_Format1 -> measData
     */
    E2SM_KPM_E2SM_KPM_IndicationMessage_Format1_t* format = (E2SM_KPM_E2SM_KPM_IndicationMessage_Format1_t*)calloc(1, sizeof(E2SM_KPM_E2SM_KPM_IndicationMessage_Format1_t));
    ASN_STRUCT_RESET(asn_DEF_E2SM_KPM_E2SM_KPM_IndicationMessage_Format1, format);
    format->subscriptID = subscriptionID;
    format->measInfoList = meas_info_list;
    format->measData = *meas_data;

    /*
     * IndicationMessage -> IndicationMessage_Format1
     */
    E2SM_KPM_E2SM_KPM_IndicationMessage_t* indicationmessage = 
                                (E2SM_KPM_E2SM_KPM_IndicationMessage_t*)calloc(1, sizeof(E2SM_KPM_E2SM_KPM_IndicationMessage_t));
    indicationmessage->indicationMessage_formats.present = 
                                    E2SM_KPM_E2SM_KPM_IndicationMessage__indicationMessage_formats_PR_indicationMessage_Format1;
    indicationmessage->indicationMessage_formats.choice.indicationMessage_Format1 = *format;
    
    return indicationmessage;
}

static void generate_e2apv1_indication_request_parameterized(E2AP_E2AP_PDU_t *e2ap_pdu,
        long requestorId, long instanceId, long ranFunctionId, long actionId,
        long seqNum, uint8_t *ind_header_buf, int header_length,
        uint8_t *ind_message_buf, int message_length)
{

    e2ap_pdu->present = E2AP_E2AP_PDU_PR_initiatingMessage;

    E2AP_InitiatingMessage_t* initmsg = &e2ap_pdu->choice.initiatingMessage;
    initmsg->procedureCode = E2AP_ProcedureCode_id_RICindication;
    initmsg->criticality = E2AP_Criticality_ignore;
    initmsg->value.present = E2AP_InitiatingMessage__value_PR_RICindication;

    /*
     * Encode RICrequestID IE
     */
    E2AP_RICindication_IEs_t *ricrequestid_ie = (E2AP_RICindication_IEs_t*)calloc(1, sizeof(E2AP_RICindication_IEs_t));
    ricrequestid_ie->id = E2AP_ProtocolIE_ID_id_RICrequestID;
    ricrequestid_ie->criticality = 0;
    ricrequestid_ie->value.present = E2AP_RICsubscriptionRequest_IEs__value_PR_RICrequestID;
    ricrequestid_ie->value.choice.RICrequestID.ricRequestorID = requestorId;
    ricrequestid_ie->value.choice.RICrequestID.ricInstanceID = instanceId;
    ASN_SEQUENCE_ADD(&initmsg->value.choice.RICindication.protocolIEs.list, ricrequestid_ie);

    /*
     * Encode RANfunctionID IE
     */
    E2AP_RICindication_IEs_t *ricind_ies2 = (E2AP_RICindication_IEs_t*)calloc(1, sizeof(E2AP_RICindication_IEs_t));
    ricind_ies2->id = E2AP_ProtocolIE_ID_id_RANfunctionID;
    ricind_ies2->criticality = 0;
    ricind_ies2->value.present = E2AP_RICindication_IEs__value_PR_RANfunctionID;
    ricind_ies2->value.choice.RANfunctionID = ranFunctionId;
    ASN_SEQUENCE_ADD(&initmsg->value.choice.RICindication.protocolIEs.list, ricind_ies2);


    /*
     * Encode RICactionID IE
     */
    E2AP_RICindication_IEs_t *ricind_ies3 = (E2AP_RICindication_IEs_t*)calloc(1, sizeof(E2AP_RICindication_IEs_t));
    ricind_ies3->id = E2AP_ProtocolIE_ID_id_RICactionID;
    ricind_ies3->criticality = 0;
    ricind_ies3->value.present = E2AP_RICindication_IEs__value_PR_RICactionID;
    ricind_ies3->value.choice.RICactionID = actionId;
    ASN_SEQUENCE_ADD(&initmsg->value.choice.RICindication.protocolIEs.list, ricind_ies3);


    /*
     * Encode RICindicationSN IE
     */
    E2AP_RICindication_IEs_t *ricind_ies4 = (E2AP_RICindication_IEs_t*)calloc(1, sizeof(E2AP_RICindication_IEs_t));
    ricind_ies4->id = E2AP_ProtocolIE_ID_id_RICindicationSN;
    ricind_ies4->criticality = 0;
    ricind_ies4->value.present = E2AP_RICindication_IEs__value_PR_RICindicationSN;
    ricind_ies4->value.choice.RICindicationSN = seqNum;
    ASN_SEQUENCE_ADD(&initmsg->value.choice.RICindication.protocolIEs.list, ricind_ies4);

    /*
     * Encode RICindicationType IE
     */
    E2AP_RICindication_IEs_t *ricind_ies5 = (E2AP_RICindication_IEs_t*)calloc(1, sizeof(E2AP_RICindication_IEs_t));
    ricind_ies5->id = E2AP_ProtocolIE_ID_id_RICindicationType;
    ricind_ies5->criticality = 0;
    ricind_ies5->value.present = E2AP_RICindication_IEs__value_PR_RICindicationType;
    ricind_ies5->value.choice.RICindicationType = E2AP_RICindicationType_report;
    ASN_SEQUENCE_ADD(&initmsg->value.choice.RICindication.protocolIEs.list, ricind_ies5);


#if 0
    //uint8_t *buf2 = (uint8_t *)"reportheader";
    OCTET_STRING_t *hdr_str = (OCTET_STRING_t*)calloc(1,sizeof(OCTET_STRING_t));

    hdr_str->buf = (uint8_t*)calloc(1,header_length);
    hdr_str->size = header_length;
    memcpy(hdr_str->buf, ind_header_buf, header_length);
#endif

    /*
     * Encode RICindicationHeader IE
     */
    E2AP_RICindication_IEs_t *ricind_ies6 = (E2AP_RICindication_IEs_t*)calloc(1, sizeof(E2AP_RICindication_IEs_t));
    ricind_ies6->id = E2AP_ProtocolIE_ID_id_RICindicationHeader;
    ricind_ies6->criticality = 0;
    ricind_ies6->value.present = E2AP_RICindication_IEs__value_PR_RICindicationHeader;
    ricind_ies6->value.choice.RICindicationHeader.buf = (uint8_t*)calloc(1, header_length);
    ricind_ies6->value.choice.RICindicationHeader.size = header_length;
    memcpy(ricind_ies6->value.choice.RICindicationHeader.buf, ind_header_buf, header_length);
    ASN_SEQUENCE_ADD(&initmsg->value.choice.RICindication.protocolIEs.list, ricind_ies6);

    /*
     * Encode RICindicationMessage IE
     */
    E2AP_RICindication_IEs_t *ricind_ies7 = (E2AP_RICindication_IEs_t*)calloc(1, sizeof(E2AP_RICindication_IEs_t));
    ricind_ies7->id = E2AP_ProtocolIE_ID_id_RICindicationMessage;
    ricind_ies7->criticality = 0;
    ricind_ies7->value.present = E2AP_RICindication_IEs__value_PR_RICindicationMessage;
    ricind_ies7->value.choice.RICindicationMessage.buf = (uint8_t*)calloc(1, 8192);
    memcpy(ricind_ies7->value.choice.RICindicationMessage.buf, ind_message_buf, message_length);
    ricind_ies7->value.choice.RICindicationMessage.size = message_length;
    ASN_SEQUENCE_ADD(&initmsg->value.choice.RICindication.protocolIEs.list, ricind_ies7);


    char *error_buf = (char*)calloc(300, sizeof(char));
    size_t errlen;

    asn_check_constraints(&asn_DEF_E2AP_E2AP_PDU, e2ap_pdu, error_buf, &errlen);
    printf("error length %zu\n", errlen);
    printf("error buf %s\n", error_buf);

    xer_fprint(stderr, &asn_DEF_E2AP_E2AP_PDU, e2ap_pdu);
}

static int e2ap_asn1c_encode_pdu(E2AP_E2AP_PDU_t* pdu, unsigned char **buffer)
{
    int len;

    *buffer = NULL;
    assert(pdu != NULL);
    assert(buffer != NULL);

    len = aper_encode_to_new_buffer(&asn_DEF_E2AP_E2AP_PDU, 0, pdu, (void **)buffer);

    if (len < 0)  {
        RIC_AGENT_ERROR("Unable to aper encode");
        exit(1);
    }
    else {
        RIC_AGENT_INFO("Encoded succesfully, encoded size = %d", len);
    }

    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_E2AP_E2AP_PDU, pdu);

    return len;
}

void encode_e2sm_kpm_indication_header(ranid_t ranid, E2SM_KPM_E2SM_KPM_IndicationHeader_t *ihead) 
{
    e2node_type_t node_type;
    ihead->indicationHeader_formats.present = E2SM_KPM_E2SM_KPM_IndicationHeader__indicationHeader_formats_PR_indicationHeader_Format1;

    E2SM_KPM_E2SM_KPM_IndicationHeader_Format1_t* ind_header = &ihead->indicationHeader_formats.choice.indicationHeader_Format1;

    /* KPM Node ID */
    ind_header->kpmNodeID = (E2SM_KPM_GlobalKPMnode_ID_t *)calloc(1,sizeof(E2SM_KPM_GlobalKPMnode_ID_t));
    ind_header->kpmNodeID->present = E2SM_KPM_GlobalKPMnode_ID_PR_eNB;

    node_type = e2_conf[ranid]->e2node_type;
    
    if (node_type == E2NODE_TYPE_ENB_CU) 
    {
        MCC_MNC_TO_PLMNID(
                e2_conf[ranid]->mcc,
                e2_conf[ranid]->mnc,
                e2_conf[ranid]->mnc_digit_length,
                &ind_header->kpmNodeID->choice.eNB.global_eNB_ID.pLMN_Identity);
    
        ind_header->kpmNodeID->choice.eNB.global_eNB_ID.eNB_ID.present = E2SM_KPM_ENB_ID_PR_macro_eNB_ID; 
    
        MACRO_ENB_ID_TO_BIT_STRING(
                e2_conf[ranid]->cell_identity,
                &ind_header->kpmNodeID->choice.eNB.global_eNB_ID.eNB_ID.choice.macro_eNB_ID);
    }

    /* Collect Start Time Stamp */
    /* Encoded in the same format as the first four octets of the 64-bit timestamp format as defined in section 6 of IETF RFC 5905 */
    ind_header->colletStartTime.buf = (uint8_t *)strdup(""); //TBD
    ind_header->colletStartTime.size = strlen(""); //TBD
    xer_fprint(stderr, &asn_DEF_E2SM_KPM_E2SM_KPM_IndicationHeader, ihead);
}