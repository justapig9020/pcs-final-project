/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation;
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef NR_MAC_H
#define NR_MAC_H

#include "nr-mac-pdu-header.h"
#include "nr-phy-mac-common.h"

namespace ns3 {

/**
 * \ingroup ue-mac
 * \ingroup gnb-mac
 * \brief Used to track the MAC PDU with the slot in which has to go, and the DCI
 * that generated it
 *
 */
struct NrMacPduInfo
{
  /**
   * \brief Construct a NrMacPduInfo
   * \param sfn SfnSf of the PDU
   * \param numRlcPdu Number of PDU inside this struct
   * \param dci DCI of the PDU
   */
  NrMacPduInfo (SfnSf sfn, uint8_t numRlcPdu, std::shared_ptr<DciInfoElementTdma> dci) :
    m_sfnSf (sfn), m_numRlcPdu (numRlcPdu), m_dci (dci)
  {
    m_pdu = Create<Packet> ();
    m_macHeader = NrMacPduHeader ();
  }

  SfnSf m_sfnSf;                  //!< SfnSf of the PDU
  uint8_t m_numRlcPdu;            //!< Number of RLC PDU
  Ptr<Packet> m_pdu;              //!< The data of the PDU
  NrMacPduHeader m_macHeader;     //!< The MAC header
  std::shared_ptr<DciInfoElementTdma> m_dci; //!< The DCI
};

} // namespace ns3
#endif // NR_MAC_H