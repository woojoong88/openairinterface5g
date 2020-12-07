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

#include "intertask_interface.h"

#include "E2AP_Cause.h"
#include "E2AP_ProtocolIE-Field.h"
#include "E2AP_InitiatingMessage.h"
#include "E2AP_SuccessfulOutcome.h"
#include "E2AP_UnsuccessfulOutcome.h"
#include "E2AP_E2setupRequest.h"

#include "ric_agent_common.h"
#include "ric_agent_defs.h"
#include "e2ap_common.h"
#include "e2ap_generate_messages.h"
#include "e2ap_encoder.h"
#include "e2ap_decoder.h"
#include "ric_agent_rrc.h"
#include "e2sm_common.h"

#include <string.h>

#include "assertions.h"
#include "conversions.h"
#include "common/ngran_types.h"

int e2ap_generate_e2_setup_request(ric_agent_info_t *ric,
				   uint8_t **buffer,uint32_t *len)
{
  E2AP_E2AP_PDU_t pdu;
  E2AP_E2setupRequest_t *req;
  E2AP_E2setupRequestIEs_t *ie;
  E2AP_RANfunction_ItemIEs_t *ran_function_item_ie;
  ric_ran_function_t *func;
  uint16_t mcc,mnc;
  uint8_t mnc_digit_len;
  ngran_node_t node_type;
  int ret;
  uint32_t nb_id;
  unsigned int i;

  DevAssert(ric != NULL);
  DevAssert(buffer != NULL && len != NULL);

  ric_rrc_get_node_type(ric->ranid,&node_type);
  if (!(node_type == ngran_eNB || node_type == ngran_ng_eNB
	|| node_type == ngran_gNB || node_type == ngran_eNB_CU)) {
    RIC_AGENT_ERROR("unsupported eNB/gNB ngran_node_t %d; aborting!\n",
		    node_type);
    return 1;
  }
  ret = ric_rrc_get_mcc_mnc(ric->ranid,0,&mcc,&mnc,&mnc_digit_len);
  if (ret) {
    RIC_AGENT_ERROR("unable to get plmn for ranid %u!\n",ric->ranid);
    return 1;
  }
  ret = ric_rcc_get_nb_id(ric->ranid,&nb_id);
  if (ret) {
    RIC_AGENT_ERROR("unable to get nb id for ranid %u!\n",ric->ranid);
    return 1;
  }

  memset(&pdu,0,sizeof(pdu));
  pdu.present = E2AP_E2AP_PDU_PR_initiatingMessage;
  pdu.choice.initiatingMessage.procedureCode = E2AP_ProcedureCode_id_E2setup;
  pdu.choice.initiatingMessage.criticality = E2AP_Criticality_reject;
  pdu.choice.initiatingMessage.value.present = E2AP_InitiatingMessage__value_PR_E2setupRequest;
  req = &pdu.choice.initiatingMessage.value.choice.E2setupRequest;

  ie = (E2AP_E2setupRequestIEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_GlobalE2node_ID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_E2setupRequestIEs__value_PR_GlobalE2node_ID;
  if (node_type == ngran_eNB || node_type == ngran_eNB_CU) {
    ie->value.choice.GlobalE2node_ID.present = E2AP_GlobalE2node_ID_PR_eNB;
    MCC_MNC_TO_PLMNID(
      mcc,mnc,mnc_digit_len,
      &ie->value.choice.GlobalE2node_ID.choice.eNB.global_eNB_ID.pLMN_Identity);
    ie->value.choice.GlobalE2node_ID.choice.eNB.global_eNB_ID.eNB_ID.present = \
      E2AP_ENB_ID_PR_macro_eNB_ID;
    MACRO_ENB_ID_TO_BIT_STRING(
      nb_id,
      &ie->value.choice.GlobalE2node_ID.choice.eNB.global_eNB_ID.eNB_ID.choice.macro_eNB_ID);
  }
  else if (node_type == ngran_ng_eNB) {
    ie->value.choice.GlobalE2node_ID.present = E2AP_GlobalE2node_ID_PR_ng_eNB;
    MCC_MNC_TO_PLMNID(
      mcc,mnc,mnc_digit_len,
      &ie->value.choice.GlobalE2node_ID.choice.ng_eNB.global_ng_eNB_ID.plmn_id);
    ie->value.choice.GlobalE2node_ID.choice.ng_eNB.global_ng_eNB_ID.enb_id.present = \
      E2AP_ENB_ID_Choice_PR_enb_ID_macro;
    MACRO_ENB_ID_TO_BIT_STRING(
      nb_id,
      &ie->value.choice.GlobalE2node_ID.choice.ng_eNB.global_ng_eNB_ID.enb_id.choice.enb_ID_macro);
  }
  else if (node_type == ngran_gNB) {
    ie->value.choice.GlobalE2node_ID.present = E2AP_GlobalE2node_ID_PR_gNB;
    MCC_MNC_TO_PLMNID(
      mcc,mnc,mnc_digit_len,
      &ie->value.choice.GlobalE2node_ID.choice.gNB.global_gNB_ID.plmn_id);
    ie->value.choice.GlobalE2node_ID.choice.gNB.global_gNB_ID.gnb_id.present = \
      E2AP_GNB_ID_Choice_PR_gnb_ID;
    /* XXX: GNB version? */
    MACRO_ENB_ID_TO_BIT_STRING(
      nb_id,
      &ie->value.choice.GlobalE2node_ID.choice.gNB.global_gNB_ID.gnb_id.choice.gnb_ID);
  }
  ASN_SEQUENCE_ADD(&req->protocolIEs.list,ie);

  /* "Optional" RANfunctions_List. */
  ie = (E2AP_E2setupRequestIEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionsAdded;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_E2setupRequestIEs__value_PR_RANfunctions_List;

  for (i = 0; i < ric->functions_enabled_len; ++i) {
    func = ric_agent_lookup_ran_function(ric->functions_enabled[i]);
    DevAssert(func != NULL);

    ran_function_item_ie = (E2AP_RANfunction_ItemIEs_t *) \
      calloc(1,sizeof(*ran_function_item_ie));
    ran_function_item_ie->id = E2AP_ProtocolIE_ID_id_RANfunction_Item;
    ran_function_item_ie->criticality = E2AP_Criticality_reject;
    ran_function_item_ie->value.present = \
      E2AP_RANfunction_ItemIEs__value_PR_RANfunction_Item;
    ran_function_item_ie->value.choice.RANfunction_Item.ranFunctionID = \
      func->function_id;
    ran_function_item_ie->value.choice.RANfunction_Item.ranFunctionRevision = \
      func->revision;

    ran_function_item_ie->value.choice.RANfunction_Item.ranFunctionDefinition.buf = (uint8_t *)malloc(func->enc_definition_len);
    memcpy(ran_function_item_ie->value.choice.RANfunction_Item.ranFunctionDefinition.buf,
	   func->enc_definition,func->enc_definition_len);
    ran_function_item_ie->value.choice.RANfunction_Item.ranFunctionDefinition.size = func->enc_definition_len;
    ASN_SEQUENCE_ADD(&ie->value.choice.RANfunctions_List.list,
		     ran_function_item_ie);
  }
  ASN_SEQUENCE_ADD(&req->protocolIEs.list,ie);

  if (e2ap_encode_pdu(&pdu,buffer,len) < 0) {
    E2AP_ERROR("Failed to encode E2setupRequest\n");
    return 1;
  }
  /*
  E2AP_E2AP_PDU_t pdud;
  memset(&pdud,0,sizeof(pdud));
  if (e2ap_decode_pdu(&pdud,*buffer,*len) < 0) {
    E2AP_WARN("Failed to encode E2setupRequest\n");
  }
  */

  return 0;
}

int e2ap_generate_ric_subscription_response(ric_agent_info_t *ric,
					    ric_subscription_t *rs,
					    uint8_t **buffer,uint32_t *len)
{
  E2AP_E2AP_PDU_t pdu;
  E2AP_RICsubscriptionResponse_t *out;
  E2AP_RICsubscriptionResponse_IEs_t *ie;
  E2AP_RICaction_NotAdmitted_Item_t *nai;
  ric_action_t *action;

  memset(&pdu, 0, sizeof(pdu));
  pdu.present = E2AP_E2AP_PDU_PR_successfulOutcome;
  pdu.choice.successfulOutcome.procedureCode = E2AP_ProcedureCode_id_RICsubscription;
  pdu.choice.successfulOutcome.criticality = E2AP_Criticality_reject;
  pdu.choice.successfulOutcome.value.present = E2AP_SuccessfulOutcome__value_PR_RICsubscriptionResponse;
  out = &pdu.choice.successfulOutcome.value.choice.RICsubscriptionResponse;

  ie = (E2AP_RICsubscriptionResponse_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICrequestID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionResponse_IEs__value_PR_RICrequestID;
  ie->value.choice.RICrequestID.ricRequestorID = rs->request_id;
  ie->value.choice.RICrequestID.ricInstanceID = rs->instance_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICsubscriptionResponse_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionResponse_IEs__value_PR_RANfunctionID;
  ie->value.choice.RANfunctionID = rs->function_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

#ifdef SHAD
  ie = (E2AP_RICsubscriptionResponse_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICactions_Admitted;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionResponse_IEs__value_PR_RICaction_Admitted_List;
  LIST_FOREACH(action, &rs->action_list, actions) {
    if (!action->enabled) {
      continue;
    }
    E2AP_RICaction_Admitted_Item_t *ai = (E2AP_RICaction_Admitted_Item_t *)calloc(1,sizeof(*ai));
    ai->ricActionID = action->id;
    ASN_SEQUENCE_ADD(&ie->value.choice.RICaction_Admitted_List.list,ai);
  }
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);
#endif

  ie = (E2AP_RICsubscriptionResponse_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICactions_NotAdmitted;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionResponse_IEs__value_PR_RICaction_NotAdmitted_List;
  LIST_FOREACH(action,&rs->action_list,actions) {
    if (action->enabled)
      continue;
    nai = (E2AP_RICaction_NotAdmitted_Item_t *)calloc(1,sizeof(*nai));
    nai->ricActionID = action->id;
    nai->cause.present = action->error_cause;
    switch (nai->cause.present) {
    case E2AP_Cause_PR_NOTHING:
      break;
    case E2AP_Cause_PR_ricRequest:
      nai->cause.choice.ricRequest = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_ricService:
      nai->cause.choice.ricService = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_transport:
      nai->cause.choice.transport = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_protocol:
      nai->cause.choice.protocol = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_misc:
      nai->cause.choice.misc = action->error_cause_detail;
      break;
    default:
      break;
    }
    ASN_SEQUENCE_ADD(&ie->value.choice.RICaction_NotAdmitted_List.list,nai);
  }
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  if (e2ap_encode_pdu(&pdu,buffer,len) < 0) {
    E2AP_ERROR("Failed to encode RICsubscriptionResponse\n");
    return -1;
  }

  return 0;
}

int e2ap_generate_ric_subscription_failure(ric_agent_info_t *ric,
					   ric_subscription_t *rs,
					   uint8_t **buffer,uint32_t *len)
{
  E2AP_E2AP_PDU_t pdu;
  E2AP_RICsubscriptionFailure_t *out;
  E2AP_RICsubscriptionFailure_IEs_t *ie;
  E2AP_RICaction_NotAdmitted_Item_t *ai;
  ric_action_t *action;

  memset(&pdu, 0, sizeof(pdu));
  pdu.present = E2AP_E2AP_PDU_PR_unsuccessfulOutcome;
  pdu.choice.unsuccessfulOutcome.procedureCode = E2AP_ProcedureCode_id_RICsubscription;
  pdu.choice.unsuccessfulOutcome.criticality = E2AP_Criticality_reject;
  pdu.choice.unsuccessfulOutcome.value.present = E2AP_UnsuccessfulOutcome__value_PR_RICsubscriptionFailure;
  out = &pdu.choice.unsuccessfulOutcome.value.choice.RICsubscriptionFailure;

  ie = (E2AP_RICsubscriptionFailure_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICrequestID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionFailure_IEs__value_PR_RICrequestID;
  ie->value.choice.RICrequestID.ricRequestorID = rs->request_id;
  ie->value.choice.RICrequestID.ricInstanceID = rs->instance_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICsubscriptionFailure_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionFailure_IEs__value_PR_RANfunctionID;
  ie->value.choice.RANfunctionID = rs->function_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICsubscriptionFailure_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICactions_NotAdmitted;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionFailure_IEs__value_PR_RICaction_NotAdmitted_List;
  LIST_FOREACH(action,&rs->action_list,actions) {
    ai = (E2AP_RICaction_NotAdmitted_Item_t *)calloc(1,sizeof(*ai));
    ai->ricActionID = action->id;
    ai->cause.present = action->error_cause;
    switch (ai->cause.present) {
    case E2AP_Cause_PR_NOTHING:
      break;
    case E2AP_Cause_PR_ricRequest:
      ai->cause.choice.ricRequest = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_ricService:
      ai->cause.choice.ricService = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_transport:
      ai->cause.choice.transport = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_protocol:
      ai->cause.choice.protocol = action->error_cause_detail;
      break;
    case E2AP_Cause_PR_misc:
      ai->cause.choice.misc = action->error_cause_detail;
      break;
    default:
      break;
    }
    ASN_SEQUENCE_ADD(&ie->value.choice.RICaction_NotAdmitted_List.list,ai);
  }
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  if (e2ap_encode_pdu(&pdu,buffer,len) < 0) {
    E2AP_ERROR("Failed to encode RICsubscriptionFailure\n");
    return -1;
  }

  return 0;
}

int e2ap_generate_ric_subscription_delete_response(
  ric_agent_info_t *ric,long request_id,long instance_id,
  ric_ran_function_id_t function_id,uint8_t **buffer,uint32_t *len)
{
  E2AP_E2AP_PDU_t pdu;
  E2AP_RICsubscriptionDeleteResponse_t *out;
  E2AP_RICsubscriptionDeleteResponse_IEs_t *ie;

  memset(&pdu, 0, sizeof(pdu));
  pdu.present = E2AP_E2AP_PDU_PR_unsuccessfulOutcome;
  pdu.choice.successfulOutcome.procedureCode = E2AP_ProcedureCode_id_RICsubscription;
  pdu.choice.successfulOutcome.criticality = E2AP_Criticality_reject;
  pdu.choice.successfulOutcome.value.present = E2AP_SuccessfulOutcome__value_PR_RICsubscriptionDeleteResponse;
  out = &pdu.choice.successfulOutcome.value.choice.RICsubscriptionDeleteResponse;

  ie = (E2AP_RICsubscriptionDeleteResponse_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICrequestID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionDeleteResponse_IEs__value_PR_RICrequestID;
  ie->value.choice.RICrequestID.ricRequestorID = request_id;
  ie->value.choice.RICrequestID.ricInstanceID = instance_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICsubscriptionDeleteResponse_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionDeleteResponse_IEs__value_PR_RANfunctionID;
  ie->value.choice.RANfunctionID = function_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  if (e2ap_encode_pdu(&pdu,buffer,len) < 0) {
    E2AP_ERROR("Failed to encode RICsubscriptionDeleteResponse\n");
    return -1;
  }

  return 0;
}

int e2ap_generate_ric_subscription_delete_failure(
  ric_agent_info_t *ric,long request_id,long instance_id,
  ric_ran_function_id_t function_id,long cause,long cause_detail,
  uint8_t **buffer,uint32_t *len)
{
  E2AP_E2AP_PDU_t pdu;
  E2AP_RICsubscriptionDeleteFailure_t *out;
  E2AP_RICsubscriptionDeleteFailure_IEs_t *ie;

  memset(&pdu, 0, sizeof(pdu));
  pdu.present = E2AP_E2AP_PDU_PR_unsuccessfulOutcome;
  pdu.choice.unsuccessfulOutcome.procedureCode = E2AP_ProcedureCode_id_RICsubscription;
  pdu.choice.unsuccessfulOutcome.criticality = E2AP_Criticality_reject;
  pdu.choice.unsuccessfulOutcome.value.present = E2AP_UnsuccessfulOutcome__value_PR_RICsubscriptionDeleteFailure;
  out = &pdu.choice.unsuccessfulOutcome.value.choice.RICsubscriptionDeleteFailure;

  ie = (E2AP_RICsubscriptionDeleteFailure_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICrequestID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionDeleteFailure_IEs__value_PR_RICrequestID;
  ie->value.choice.RICrequestID.ricRequestorID = request_id;
  ie->value.choice.RICrequestID.ricInstanceID = instance_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICsubscriptionDeleteFailure_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionID;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionDeleteFailure_IEs__value_PR_RANfunctionID;
  ie->value.choice.RANfunctionID = function_id;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICsubscriptionDeleteFailure_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RICactions_NotAdmitted;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICsubscriptionDeleteFailure_IEs__value_PR_Cause;
  ie->value.choice.Cause.present = cause;
  switch (cause) {
  case E2AP_Cause_PR_NOTHING:
    break;
  case E2AP_Cause_PR_ricRequest:
    ie->value.choice.Cause.choice.ricRequest = cause_detail;
    break;
  case E2AP_Cause_PR_ricService:
    ie->value.choice.Cause.choice.ricService = cause_detail;
    break;
  case E2AP_Cause_PR_transport:
    ie->value.choice.Cause.choice.transport = cause_detail;
    break;
  case E2AP_Cause_PR_protocol:
    ie->value.choice.Cause.choice.protocol = cause_detail;
    break;
  case E2AP_Cause_PR_misc:
    ie->value.choice.Cause.choice.misc = cause_detail;
    break;
  default:
    break;
  }
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  if (e2ap_encode_pdu(&pdu,buffer,len) < 0) {
    E2AP_ERROR("Failed to encode RICsubscriptionDeleteFailure\n");
    return -1;
  }

  return 0;
}

int e2ap_generate_ric_service_update(ric_agent_info_t *ric,
				     uint8_t **buffer,uint32_t *len)
{
  E2AP_E2AP_PDU_t pdu;
  E2AP_RICserviceUpdate_t *out;
  E2AP_RICserviceUpdate_IEs_t *ie;

  /*
   * NB: we never add, modify, or remove ran functions, so this is a noop.
   */

  memset(&pdu, 0, sizeof(pdu));
  pdu.present = E2AP_E2AP_PDU_PR_initiatingMessage;
  pdu.choice.initiatingMessage.procedureCode = E2AP_ProcedureCode_id_RICserviceUpdate;
  pdu.choice.initiatingMessage.criticality = E2AP_Criticality_reject;
  pdu.choice.initiatingMessage.value.present = E2AP_InitiatingMessage__value_PR_RICserviceUpdate;
  out = &pdu.choice.initiatingMessage.value.choice.RICserviceUpdate;

  ie = (E2AP_RICserviceUpdate_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionsAdded;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICserviceUpdate_IEs__value_PR_RANfunctions_List;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICserviceUpdate_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionsModified;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICserviceUpdate_IEs__value_PR_RANfunctions_List_1;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  ie = (E2AP_RICserviceUpdate_IEs_t *)calloc(1,sizeof(*ie));
  ie->id = E2AP_ProtocolIE_ID_id_RANfunctionsDeleted;
  ie->criticality = E2AP_Criticality_reject;
  ie->value.present = E2AP_RICserviceUpdate_IEs__value_PR_RANfunctionsID_List;
  ASN_SEQUENCE_ADD(&out->protocolIEs.list,ie);

  if (e2ap_encode_pdu(&pdu,buffer,len) < 0) {
    E2AP_ERROR("Failed to encode RICserviceUpdate\n");
    return -1;
  }

  return 0;
}

int e2ap_generate_reset_response(ric_agent_info_t *ric,
				 uint8_t **buffer,uint32_t *len)
{
  E2AP_E2AP_PDU_t pdu;

  memset(&pdu, 0, sizeof(pdu));
  pdu.present = E2AP_E2AP_PDU_PR_successfulOutcome;
  pdu.choice.successfulOutcome.procedureCode = E2AP_ProcedureCode_id_Reset;
  pdu.choice.successfulOutcome.criticality = E2AP_Criticality_reject;
  pdu.choice.successfulOutcome.value.present = E2AP_SuccessfulOutcome__value_PR_ResetResponse;

  if (e2ap_encode_pdu(&pdu,buffer,len) < 0) {
    E2AP_ERROR("Failed to encode ResetResponse\n");
    return -1;
  }

  return 0;
}