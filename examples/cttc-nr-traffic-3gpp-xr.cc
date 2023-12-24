
//
// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/boolean.h"
#include "ns3/config-store-module.h"
#include "ns3/config-store.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-enb-rrc.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/packet-sink.h"
#include "ns3/point-to-point-module.h"
#include "ns3/xr-traffic-mixer-helper.h"
#include "ns3/object-factory.h"

#include <vector>

/**
 * \file cttc-nr-traffic-3gpp-xr.cc
 * \ingroup examples
 * \brief Simple topology consisting of 1 GNB and various UEs.
 *  Can be configured with different 3GPP XR traffic generators (by using
 *  XR traffic mixer helper).
 *
 * To run the simulation with the default configuration one shall run the
 * following in the command line:
 *
 * ./ns3 run cttc-nr-traffic-generator-3gpp-xr
 *
 */

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("CttcNrTraffic3gppXr");

void
WriteBytesSent(Ptr<TrafficGenerator> trafficGenerator,
               uint64_t* previousBytesSent,
               uint64_t* previousWindowBytesSent,
               enum NrXrConfig NrXrConfig,
               std::ofstream* outFileTx)
{
    uint64_t totalBytesSent = trafficGenerator->GetTotalBytes();
    (*outFileTx) << "\n"
                 << Simulator::Now().GetMilliSeconds() << "\t" << *previousWindowBytesSent
                 << std::endl;
    (*outFileTx) << "\n"
                 << Simulator::Now().GetMilliSeconds() << "\t"
                 << totalBytesSent - *previousBytesSent << std::endl;

    *previousWindowBytesSent = totalBytesSent - *previousBytesSent;
    *previousBytesSent = totalBytesSent;
};

void
WriteBytesReceived(Ptr<PacketSink> packetSink, uint64_t* previousBytesReceived)
{
    uint64_t totalBytesReceived = packetSink->GetTotalRx();
    *previousBytesReceived = totalBytesReceived;
};

void
ConfigureXrApp(NodeContainer& ueContainer,
               uint32_t i,
               Ipv4InterfaceContainer& ueIpIface,
               enum NrXrConfig config,
               uint16_t port,
               std::string transportProtocol,
               NodeContainer& remoteHostContainer,
               NetDeviceContainer& ueNetDev,
               Ptr<NrHelper> nrHelper,
               EpsBearer& bearer,
               Ptr<EpcTft> tft,
               bool isMx1,
               std::vector<Ptr<EpcTft>>& tfts,
               ApplicationContainer& serverApps,
               ApplicationContainer& clientApps,
               ApplicationContainer& pingApps,
               ApplicationContainer& updaterApps,
	       Ptr<ns3::FlowMonitor> monitor,
                Ptr<Ipv4FlowClassifier> classifier,
               TrafficGenerator3gppGenericVideo::LoopbackAlgType loopbackAlgType=TrafficGenerator3gppGenericVideo::LoopbackAlgType::ADJUST_IPA_TIME)
{
    XrTrafficMixerHelper trafficMixerHelper;
    Ipv4Address ipAddress = ueIpIface.GetAddress(i, 0);
    trafficMixerHelper.ConfigureXr(config);
    auto it = XrPreconfig.find(config);

    std::vector<Address> addresses;
    std::vector<InetSocketAddress> localAddresses;
    for (uint j = 0; j < it->second.size(); j++)
    {
        addresses.emplace_back(InetSocketAddress(ipAddress, port + j));
        // The sink will always listen to the specified ports
        localAddresses.emplace_back(InetSocketAddress(Ipv4Address::GetAny(), port + j));
    }

    ApplicationContainer currentUeClientApps;
    ApplicationContainer trafficGeneratorApps = trafficMixerHelper.Install(transportProtocol, addresses, remoteHostContainer.Get(0));

    std::cout << "TrafficGeneratorApps size: " << trafficGeneratorApps.GetN() << std::endl;
    for (uint j = 0; j < trafficGeneratorApps.GetN(); j++) {
        // std::cout << "App name: " << trafficGeneratorApps.Get(j)->GetTypeId() << std::endl;
        // std::cout << "Target ID: " << TrafficGenerator3gppGenericVideo::GetTypeId() << std::endl;
        Ptr<TrafficGenerator3gppGenericVideo> videoApp = DynamicCast<TrafficGenerator3gppGenericVideo>(trafficGeneratorApps.Get(j));

        if (videoApp) {
            videoApp->SetLoopbackAlgType(loopbackAlgType);

            ObjectFactory factory;
            factory.SetTypeId(LoopbackUpdater::GetTypeId());
            Ptr<LoopbackUpdater> updater = factory.Create<LoopbackUpdater>();

            updater->setTrafficGenerator(videoApp);
            updater->setMonitor(monitor);
            updater->setWindowInSeconds(0.5);
            updater->setClassifier(classifier);
            updater->setDestIP(ipAddress);

            remoteHostContainer.Get(0)->AddApplication(updater);
            updaterApps.Add(updater);
        }
    }

    currentUeClientApps.Add(trafficGeneratorApps);

    // Seed the ARP cache by pinging early in the simulation
    // This is a workaround until a static ARP capability is provided
    PingHelper ping(ipAddress);
    pingApps.Add(ping.Install(remoteHostContainer));

    Ptr<NetDevice> ueDevice = ueNetDev.Get(i);
    // Activate a dedicated bearer for the traffic type per node
    nrHelper->ActivateDedicatedEpsBearer(ueDevice, bearer, tft);
    // Activate a dedicated bearer for the traffic type per node
    if (isMx1)
    {
        nrHelper->ActivateDedicatedEpsBearer(ueDevice, bearer, tft);
    }
    else
    {
        NS_ASSERT(tfts.size() >= currentUeClientApps.GetN());
        for (uint32_t j = 0; j < currentUeClientApps.GetN(); j++)
        {
            nrHelper->ActivateDedicatedEpsBearer(ueDevice, bearer, tfts[j]);
        }
    }

    for (uint32_t j = 0; j < currentUeClientApps.GetN(); j++)
    {
        PacketSinkHelper dlPacketSinkHelper(transportProtocol, localAddresses.at(j));
        Ptr<Application> packetSink = dlPacketSinkHelper.Install(ueContainer.Get(i)).Get(0);
        serverApps.Add(packetSink);
    }
    clientApps.Add(currentUeClientApps);
}

#include "ns3/core-module.h"
#include "ns3/network-module.h"

using namespace ns3;

int
main(int argc, char* argv[])
{
    // set simulation time and mobility
    uint32_t appDuration = 50000; // 10000;
    uint32_t appStartTimeMs = 500; // 400;
    uint16_t numerology = 1; // 0; // FDM
    uint16_t arUeNum = 2; // 1;
    uint16_t vrUeNum = 2; // 1;
    uint16_t cgUeNum = 2; // 1;
    double centralFrequency = 4e9;
    double bandwidth = 80e6; // 10e6;
    double txPower = 52; // 41; // GNB power
    bool isMx1 = true;
    bool useUdp = true;
    double distance = 500; // 450;
    uint32_t rngRun = 100; // 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("arUeNum", "The number of AR UEs", arUeNum);
    cmd.AddValue("vrUeNum", "The number of VR UEs", vrUeNum);
    cmd.AddValue("cgUeNum", "The number of CG UEs", cgUeNum);
    cmd.AddValue("numerology", "The numerology to be used.", numerology);
    cmd.AddValue("txPower", "Tx power to be configured to gNB", txPower);
    cmd.AddValue("frequency", "The system frequency", centralFrequency);
    cmd.AddValue("bandwidth", "The system bandwidth", bandwidth);
    cmd.AddValue("useUdp",
                 "if true, the NGMN applications will run over UDP connection, otherwise a TCP "
                 "connection will be used.",
                 useUdp);
    cmd.AddValue("distance",
                 "The radius of the disc (in meters) that the UEs will be distributed."
                 "Default value is 450m",
                 distance);
    cmd.AddValue("isMx1",
                 "if true M SDFs will be mapped to 1 DRB, otherwise the mapping will "
                 "be 1x1, i.e., 1 SDF to 1 DRB.",
                 isMx1);
    cmd.AddValue("rngRun", "Rng run random number.", rngRun);
    cmd.AddValue("appDuration", "Duration of the application in milliseconds.", appDuration);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(appDuration < 1000, "The appDuration should be at least 1000ms.");
    NS_ABORT_MSG_IF(
        !vrUeNum && !arUeNum && !cgUeNum,
        "Activate at least one type of XR traffic by configuring the number of XR users");

    uint32_t simTimeMs = appStartTimeMs + appDuration + 2000;

    // Set simulation run number
    SeedManager::SetRun(rngRun);

    // setup the nr simulation

    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    // simple band configuration and initialize
    // original band config
    /*
    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(centralFrequency,
                                                   bandwidth,
                                                   1,
                                                   BandwidthPartInfo::UMa_LoS);

    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    nrHelper->InitializeOperationBand(&band);
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});
    */

    // TODO: Setup the config (NR)
    std::cout << "Setup NR config" << std::endl;

    nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(txPower));
    nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(numerology));
    nrHelper->SetGnbPhyAttribute("NoiseFigure", DoubleValue(5));

    nrHelper->SetUePhyAttribute("TxPower", DoubleValue(26)); // set UE TX power
    // nrHelper->SetUePhyAttribute("TxPower", DoubleValue(23));
    
    nrHelper->SetUePhyAttribute("NoiseFigure", DoubleValue(7));

    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(999999999));
    Config::SetDefault("ns3::LteEnbRrc::EpsBearerToRlcMapping",
                       EnumValue(useUdp ? LteEnbRrc::RLC_UM_ALWAYS : LteEnbRrc::RLC_AM_ALWAYS));

    // TODO: Setup the config (NR)
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<ThreeGppAntennaModel>()));
    nrHelper->SetGnbAntennaAttribute("AntennaHorizontalSpacing", DoubleValue(0.5));
    nrHelper->SetGnbAntennaAttribute("AntennaVerticalSpacing", DoubleValue(0.8));
    nrHelper->SetGnbAntennaAttribute("DowntiltAngle", DoubleValue(0 * M_PI / 180.0));
    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));

    // Beamforming method
    std::cout << "Setup Beamforming config" << std::endl;

    Ptr<IdealBeamformingHelper> idealBeamformingHelper = CreateObject<IdealBeamformingHelper>();
    idealBeamformingHelper->SetAttribute("BeamformingMethod",
                                         TypeIdValue(DirectPathBeamforming::GetTypeId()));
    nrHelper->SetBeamformingHelper(idealBeamformingHelper);

    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    epcHelper->SetAttribute("S1uLinkDelay", TimeValue(MilliSeconds(0)));

    // my additional setting
    std::cout << "Setup Sechedule config" << std::endl;
	
    std::stringstream schedulerType;
    std::string subType;
    std::string sched;

    subType = "Tdma";
    sched = "Qos";
    schedulerType << "ns3::NrMacScheduler" << subType << sched;
    std::cout << "SchedulerType: " << schedulerType.str() << std::endl;
    nrHelper->SetSchedulerTypeId(TypeId::LookupByName(schedulerType.str()));

    std::cout << "Setup Error Model config" << std::endl;

    uint32_t mcs = 28;
    // nrHelper->SetSchedulerAttribute("FixedMcsDl", BooleanValue(true));
    // nrHelper->SetSchedulerAttribute("FixedMcsUl", BooleanValue(true));
    nrHelper->SetSchedulerAttribute("FixedMcsDl", BooleanValue(false));
    nrHelper->SetSchedulerAttribute("FixedMcsUl", BooleanValue(false));
    nrHelper->SetSchedulerAttribute("StartingMcsDl", UintegerValue(mcs));
    nrHelper->SetSchedulerAttribute("StartingMcsUl", UintegerValue(mcs));

    std::string errorModel = "ns3::NrEesmIrT1";
    Config::SetDefault("ns3::NrAmc::ErrorModelType", TypeIdValue(TypeId::LookupByName(errorModel)));
    Config::SetDefault("ns3::NrAmc::AmcModel",
                       EnumValue(NrAmc::ErrorModel));

    // "Error model type: ns3::NrEesmCcT1, ns3::NrEesmCcT2, ns3::NrEesmIrT1, "
    // "ns3::NrEesmIrT2, ns3::NrLteMiErrorModel"
    
    nrHelper->SetGnbDlAmcAttribute(
        "AmcModel",
        EnumValue(NrAmc::ErrorModel)); // NrAmc::ShannonModel or NrAmc::ErrorModel
    nrHelper->SetGnbUlAmcAttribute(
        "AmcModel",
        EnumValue(NrAmc::ErrorModel)); // NrAmc::ShannonModel or NrAmc::ErrorModel
    nrHelper->SetUlErrorModel(errorModel);
    nrHelper->SetDlErrorModel(errorModel);

    std::unique_ptr<ComponentCarrierInfo> cc0(new ComponentCarrierInfo());
    std::unique_ptr<BandwidthPartInfo> bwp0(new BandwidthPartInfo());
    std::unique_ptr<BandwidthPartInfo> bwp1(new BandwidthPartInfo());

    std::unique_ptr<ComponentCarrierInfo> cc1(new ComponentCarrierInfo());
    std::unique_ptr<BandwidthPartInfo> bwp2(new BandwidthPartInfo());

    const uint8_t numContiguousCcs = 4; // 4 CCs per Band

    // Create the configuration for the CcBwpHelper
    std::cout << "Setup CcBwpHelper config" << std::endl;

    BandwidthPartInfoPtrVector allBwps;
    CcBwpCreator ccBwpCreator;

    OperationBandInfo band;
    CcBwpCreator::SimpleOperationBandConf bandConf(centralFrequency,
					       bandwidth,
					       numContiguousCcs,
					       BandwidthPartInfo::UMi_StreetCanyon_LoS);

    bandConf.m_numBwp = 1; // 1 BWP per CC

    // By using the configuration created, it is time to make the operation band
    band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);

    nrHelper->InitializeOperationBand(&band);
    allBwps = CcBwpCreator::GetAllBwps({band});

    // Initialize nrHelper
    nrHelper->Initialize();

    // Init Nodes (gNB + UEs)
    NodeContainer gNbNodes;
    NodeContainer ueNodes;
    MobilityHelper mobility;
    // TODO: Mobility model. We should set distance between gNB and UEs here
    //      Also, we should find out the correct position allocators
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    const double gNbHeight = 25;
    const double ueHeight = 1.5;

    std::cout << "Create Nodes config" << std::endl;

    gNbNodes.Create(1);
    ueNodes.Create(arUeNum + vrUeNum + cgUeNum);

    Ptr<ListPositionAllocator> bsPositionAlloc = CreateObject<ListPositionAllocator>();
    bsPositionAlloc->Add(Vector(0.0, 0.0, gNbHeight));
    mobility.SetPositionAllocator(bsPositionAlloc);
    mobility.Install(gNbNodes);

    Ptr<RandomDiscPositionAllocator> ueDiscPositionAlloc =
        CreateObject<RandomDiscPositionAllocator>();
    ueDiscPositionAlloc->SetX(distance);
    ueDiscPositionAlloc->SetY(0.0);
    ueDiscPositionAlloc->SetZ(ueHeight);
    mobility.SetPositionAllocator(ueDiscPositionAlloc);

    for (uint32_t i = 0; i < ueNodes.GetN(); i++)
    {
        mobility.Install(ueNodes.Get(i));
    }

    /*
     * Create various NodeContainer(s) for the different traffic types.
     * In ueArContainer, ueVrContainer, ueCgContainer, we will put
     * AR, VR, CG UEs, respectively.*/
    NodeContainer ueArContainer;
    NodeContainer ueVrContainer;
    NodeContainer ueCgContainer;

    for (auto j = 0; j < arUeNum; ++j)
    {
        Ptr<Node> ue = ueNodes.Get(j);
        ueArContainer.Add(ue);
    }
    for (auto j = arUeNum; j < arUeNum + vrUeNum; ++j)
    {
        Ptr<Node> ue = ueNodes.Get(j);
        ueVrContainer.Add(ue);
    }
    for (auto j = arUeNum + vrUeNum; j < arUeNum + vrUeNum + cgUeNum; ++j)
    {
        Ptr<Node> ue = ueNodes.Get(j);
        ueCgContainer.Add(ue);
    }

    NetDeviceContainer gNbNetDev = nrHelper->InstallGnbDevice(gNbNodes, allBwps);
    NetDeviceContainer ueArNetDev = nrHelper->InstallUeDevice(ueArContainer, allBwps);
    NetDeviceContainer ueVrNetDev = nrHelper->InstallUeDevice(ueVrContainer, allBwps);
    NetDeviceContainer ueCgNetDev = nrHelper->InstallUeDevice(ueCgContainer, allBwps);

    int64_t randomStream = 1;
    randomStream += nrHelper->AssignStreams(gNbNetDev, randomStream);
    randomStream += nrHelper->AssignStreams(ueArNetDev, randomStream);
    randomStream += nrHelper->AssignStreams(ueVrNetDev, randomStream);
    randomStream += nrHelper->AssignStreams(ueCgNetDev, randomStream);

    for (auto it = gNbNetDev.Begin(); it != gNbNetDev.End(); ++it)
    {
        DynamicCast<NrGnbNetDevice>(*it)->UpdateConfig();
    }
    for (auto it = ueArNetDev.Begin(); it != ueArNetDev.End(); ++it)
    {
        DynamicCast<NrUeNetDevice>(*it)->UpdateConfig();
    }
    for (auto it = ueVrNetDev.Begin(); it != ueVrNetDev.End(); ++it)
    {
        DynamicCast<NrUeNetDevice>(*it)->UpdateConfig();
    }
    for (auto it = ueCgNetDev.Begin(); it != ueCgNetDev.End(); ++it)
    {
        DynamicCast<NrUeNetDevice>(*it)->UpdateConfig();
    }

    // create the internet and install the IP stack on the UEs
    // get SGW/PGW and create a single RemoteHost
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    // connect a remoteHost to pgw. Setup routing too
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("20Mb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(1000));
    p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.000)));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);
    internet.Install(ueNodes);

    Ipv4InterfaceContainer ueArIpIface;
    Ipv4InterfaceContainer ueVrIpIface;
    Ipv4InterfaceContainer ueCgIpIface;

    ueArIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueArNetDev));
    ueVrIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueVrNetDev));
    ueCgIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueCgNetDev));

    // Set the default gateway for the UEs
    for (uint32_t j = 0; j < ueNodes.GetN(); ++j)
    {
        Ptr<Ipv4StaticRouting> ueStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(j)->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    // attach UEs to the closest eNB
    nrHelper->AttachToClosestEnb(ueArNetDev, gNbNetDev);
    nrHelper->AttachToClosestEnb(ueVrNetDev, gNbNetDev);
    nrHelper->AttachToClosestEnb(ueCgNetDev, gNbNetDev);

    // Install sink application
    ApplicationContainer serverApps;

    // configure the transport protocol to be used
    std::string transportProtocol;
    transportProtocol = useUdp == true ? "ns3::UdpSocketFactory" : "ns3::TcpSocketFactory";
    uint16_t dlPortArStart = 1121; // AR has 3 flows
    uint16_t dlPortArStop = 1124;
    uint16_t dlPortVrStart = 1131;
    uint16_t dlPortCgStart = 1141;

    // The bearer that will carry AR traffic
    EpsBearer arBearer(EpsBearer::NGBR_LOW_LAT_EMBB);
    Ptr<EpcTft> arTft = Create<EpcTft>();
    EpcTft::PacketFilter dlpfAr;
    std::vector<Ptr<EpcTft>> arTfts;

    if (isMx1)
    {
        dlpfAr.localPortStart = dlPortArStart;
        dlpfAr.localPortEnd = dlPortArStop;
        arTft->Add(dlpfAr);
    }
    else
    {
        // create 3 xrTfts for 1x1 mapping
        for (uint32_t i = 0; i < 3; i++)
        {
            Ptr<EpcTft> tempTft = Create<EpcTft>();
            dlpfAr.localPortStart = dlPortArStart + i;
            dlpfAr.localPortEnd = dlPortArStart + i;
            tempTft->Add(dlpfAr);
            arTfts.emplace_back(tempTft);
        }
    }
    // The bearer that will carry VR traffic
    EpsBearer vrBearer(EpsBearer::NGBR_LOW_LAT_EMBB);

    Ptr<EpcTft> vrTft = Create<EpcTft>();
    EpcTft::PacketFilter dlpfVr;
    dlpfVr.localPortStart = dlPortVrStart;
    dlpfVr.localPortEnd = dlPortVrStart;
    vrTft->Add(dlpfVr);

    // The bearer that will carry CG traffic
    EpsBearer cgBearer(EpsBearer::NGBR_LOW_LAT_EMBB);

    Ptr<EpcTft> cgTft = Create<EpcTft>();
    EpcTft::PacketFilter dlpfCg;
    dlpfCg.localPortStart = dlPortCgStart;
    dlpfCg.localPortEnd = dlPortCgStart;
    cgTft->Add(dlpfCg);

    // monitor
    /*
    */
    FlowMonitorHelper flowmonHelper;
    NodeContainer endpointNodes;
    endpointNodes.Add(remoteHost);
    endpointNodes.Add(ueNodes);

    Ptr<ns3::FlowMonitor> monitor = flowmonHelper.Install(endpointNodes);
    monitor->SetAttribute("DelayBinWidth", DoubleValue(0.0001));
    monitor->SetAttribute("JitterBinWidth", DoubleValue(0.001));
    monitor->SetAttribute("PacketSizeBinWidth", DoubleValue(20));
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());

    // Install traffic generators
    ApplicationContainer clientApps;
    ApplicationContainer pingApps;
    ApplicationContainer updaterApps;

    std::ostringstream xrFileTag;

    for (uint32_t i = 0; i < ueArContainer.GetN(); ++i)
    {
        std::cout << "Configure XR app for AR" << std::endl;
        ConfigureXrApp(ueArContainer,
                       i,
                       ueArIpIface,
                       AR_M3,
                       dlPortArStart,
                       transportProtocol,
                       remoteHostContainer,
                       ueArNetDev,
                       nrHelper,
                       arBearer,
                       arTft,
                       isMx1,
                       arTfts,
                       serverApps,
                       clientApps,
                       pingApps,
                       updaterApps,
		       monitor,
               classifier
                       );
    }
    // TODO for VR and CG of 2 flows Tfts and isMx1 have to be set. Currently they are
    // hardcoded for 1 flow
    for (uint32_t i = 0; i < ueVrContainer.GetN(); ++i)
    {
        std::cout << "Configure XR app for VR" << std::endl;
        ConfigureXrApp(ueVrContainer,
                       i,
                       ueVrIpIface,
                       VR_DL1,
                       dlPortVrStart,
                       transportProtocol,
                       remoteHostContainer,
                       ueVrNetDev,
                       nrHelper,
                       vrBearer,
                       vrTft,
                       1,
                       arTfts,
                       serverApps,
                       clientApps,
                       pingApps,
                       updaterApps,
		       monitor,
               classifier
                       );
    }
    for (uint32_t i = 0; i < ueCgContainer.GetN(); ++i)
    {
        std::cout << "Configure XR app for CG" << std::endl;
        ConfigureXrApp(ueCgContainer,
                       i,
                       ueCgIpIface,
                       CG_DL1,
                       dlPortCgStart,
                       transportProtocol,
                       remoteHostContainer,
                       ueCgNetDev,
                       nrHelper,
                       cgBearer,
                       cgTft,
                       1,
                       arTfts,
                       serverApps,
                       clientApps,
                       pingApps,
                       updaterApps,
		       monitor,
               classifier
                       );
    }

    pingApps.Start(MilliSeconds(100));
    pingApps.Stop(MilliSeconds(appStartTimeMs));

    // start server and client apps
    serverApps.Start(MilliSeconds(appStartTimeMs));
    clientApps.Start(MilliSeconds(appStartTimeMs));
    updaterApps.Start(MilliSeconds(appStartTimeMs));
    serverApps.Stop(MilliSeconds(simTimeMs));
    clientApps.Stop(MilliSeconds(appStartTimeMs + appDuration));
    updaterApps.Stop(MilliSeconds(simTimeMs));

    Simulator::Stop(MilliSeconds(simTimeMs));
    Simulator::Run();

    monitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double averageFlowThroughput = 0.0;
    double averageFlowDelay = 0.0;

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::stringstream protoStream;
        protoStream << (uint16_t)t.protocol;
        if (t.protocol == 6)
        {
            protoStream.str("TCP");
        }
        if (t.protocol == 17)
        {
            protoStream.str("UDP");
        }

        Time txDuration = MilliSeconds(appDuration);
        std::cout << "Flow " << i->first << " (" << t.sourceAddress << ":" << t.sourcePort << " -> "
                  << t.destinationAddress << ":" << t.destinationPort << ") proto "
                  << protoStream.str() << "\n";
        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
        std::cout << "  TxOffered:  "
                  << ((i->second.txBytes * 8.0) / txDuration.GetSeconds()) * 1e-6 << " Mbps\n";
        std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";

        if (i->second.rxPackets > 0)
        {
            // Measure the duration of the flow from receiver's perspective
            Time rxDuration = i->second.timeLastRxPacket - i->second.timeFirstTxPacket;
            averageFlowThroughput += ((i->second.rxBytes * 8.0) / rxDuration.GetSeconds()) * 1e-6;
            averageFlowDelay += 1000 * i->second.delaySum.GetSeconds() / i->second.rxPackets;

            double throughput = ((i->second.rxBytes * 8.0) / rxDuration.GetSeconds()) * 1e-6;
            double delay = 1000 * i->second.delaySum.GetSeconds() / i->second.rxPackets;
            double jitter = 1000 * i->second.jitterSum.GetSeconds() / i->second.rxPackets;
            double packetLoss = 100 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets;

            std::cout << "  Throughput: " << throughput << " Mbps\n";
            std::cout << "  Mean delay:  " << delay << " ms\n";
            std::cout << "  Mean jitter:  " << jitter << " ms\n";
            std::cout << "  Packet Loss:  " << packetLoss << " %\n";
        }
        else
        {
            std::cout << "  Throughput:  0 Mbps\n";
            std::cout << "  Mean delay:  0 ms\n";
            std::cout << "  Mean upt:  0  Mbps \n";
            std::cout << "  Mean jitter: 0 ms\n";
        }
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
    }

    std::cout << "\n\n  Mean flow throughput: " << averageFlowThroughput / stats.size()
              << "Mbps \n";
    std::cout << "  Mean flow delay: " << averageFlowDelay / stats.size() << " ms\n";

    Simulator::Destroy();

    return 0;
}
