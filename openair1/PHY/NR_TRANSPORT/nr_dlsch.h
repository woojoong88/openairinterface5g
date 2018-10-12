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

/*! \file PHY/LTE_TRANSPORT/defs.h
* \brief data structures for PDSCH/DLSCH/PUSCH/ULSCH physical and transport channel descriptors (TX/RX)
* \author R. Knopp
* \date 2011
* \version 0.1
* \company Eurecom
* \email: raymond.knopp@eurecom.fr, florian.kaltenberger@eurecom.fr, oscar.tonelli@yahoo.it
* \note
* \warning
*/

#include "PHY/defs_gNB.h"

extern short nr_mod_table[NR_MOD_TABLE_SIZE_SHORT];

void nr_get_time_domain_allocation_type(nfapi_nr_config_request_t config,
                                        NR_gNB_DCI_ALLOC_t dci_alloc,
                                        NR_gNB_DLSCH_t* dlsch);

void nr_check_time_alloc(uint8_t S, uint8_t L, nfapi_nr_config_request_t config);

uint8_t nr_get_S(uint8_t row_idx, uint8_t CP, uint8_t time_alloc_type, uint8_t dmrs_typeA_position);

void nr_get_rbg_parms(NR_BWP_PARMS* bwp, uint8_t config_type);

void nr_get_rbg_list(uint32_t bitmap, uint8_t n_rbg, uint8_t* rbg_list);

void nr_get_PRG_parms(NR_BWP_PARMS* bwp, NR_gNB_DCI_ALLOC_t dci_alloc, uint8_t prb_bundling_type);

uint8_t nr_get_Qm(uint8_t Imcs, uint8_t table_idx);

uint32_t nr_get_code_rate(uint8_t Imcs, uint8_t table_idx);

void nr_pdsch_codeword_scrambling(uint32_t *in,
                         uint8_t size,
                         uint8_t q,
                         uint32_t Nid,
                         uint32_t n_RNTI,
                         uint32_t* out);

void nr_pdsch_codeword_modulation(uint32_t *in,
                         uint8_t  Qm,
                         uint32_t length,
                         uint16_t *out);

void nr_pdsch_layer_mapping(uint16_t **mod_symbs,
                         uint8_t n_codewords,
                         uint8_t n_layers,
                         uint16_t *n_symbs,
                         uint16_t **tx_layers);