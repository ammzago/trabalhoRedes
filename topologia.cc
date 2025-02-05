#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

// Definição do componente de log para depuração
NS_LOG_COMPONENT_DEFINE("WifiCsmaExample");

int main(int argc, char *argv[]) {
    // Definição de parâmetros padrão e possibilidade de alteração via linha de comando
    bool enableMobility = false; // Define se os nós Wi-Fi terão mobilidade
    uint32_t numWifiNodes = 5; // Número de nós Wi-Fi
    std::string trafficType = "CBR"; // Tipo de tráfego gerado na simulação

    // Permite modificar os parâmetros via linha de comando
    CommandLine cmd;
    cmd.AddValue("mobility", "Enable mobility (1 for true, 0 for false)", enableMobility);
    cmd.AddValue("numNodes", "Number of WiFi nodes", numWifiNodes);
    cmd.AddValue("traffic", "Traffic type (CBR, Burst, CBR_Burst)", trafficType);
    cmd.Parse(argc, argv);

    // Criação dos nós Wi-Fi (estações móveis)
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(numWifiNodes);

    // Criação do nó Access Point (AP)
    NodeContainer wifiApNode;
    wifiApNode.Create(1);

    // Criação de nós para a rede cabeada (CSMA)
    NodeContainer csmaNodes;
    csmaNodes.Add(wifiApNode.Get(0)); // O AP é parte da rede cabeada
    csmaNodes.Create(1); // Servidor cabeado

    // Configuração do canal de comunicação Wi-Fi
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // Configuração do Wi-Fi
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", 
                             "DataMode", StringValue("HtMcs1"),
                             "ControlMode", StringValue("HtMcs0"));

    // Configuração do MAC para os dispositivos Wi-Fi
    WifiMacHelper mac;
    Ssid ssid = Ssid("ns-3-ssid");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

    // Configuração da rede cabeada (CSMA)
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps")); // Largura de banda do CSMA
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560))); // Atraso de propagação
    
    NetDeviceContainer csmaDevices = csma.Install(csmaNodes);

    // Configuração da mobilidade
    MobilityHelper mobility;
    if (enableMobility) {
        mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel", 
                                  "Bounds", RectangleValue(Rectangle(-50, 50, -50, 50)),
                                  "Speed", StringValue("ns3::ConstantRandomVariable[Constant=2]"));
        mobility.Install(wifiStaNodes);
    } else {
        // Alocação de posição fixa caso a mobilidade esteja desativada
        mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                              "MinX", DoubleValue(0.0),
                              "MinY", DoubleValue(0.0),
                              "DeltaX", DoubleValue(10.0),
                              "DeltaY", DoubleValue(10.0),
                              "GridWidth", UintegerValue(3),
                              "LayoutType", StringValue("RowFirst"));

        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(wifiStaNodes);
    }

    // O AP não se move, então usa modelo de posição constante
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);

    // Instala a pilha de protocolos de Internet nos nós
    InternetStackHelper stack;
    stack.Install(wifiStaNodes);
    stack.Install(wifiApNode);
    stack.Install(csmaNodes);

    // Configuração dos endereços IP para os dispositivos
    Ipv4AddressHelper address;
    address.SetBase("192.168.0.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiInterfaces = address.Assign(staDevices);
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);

    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterfaces = address.Assign(csmaDevices);

    // Configuração do tráfego
    ApplicationContainer apps;
    uint16_t serverPort = 9;
    
    if (trafficType == "CBR" || trafficType == "CBR_Burst") {
        OnOffHelper cbrApp("ns3::UdpSocketFactory", InetSocketAddress(csmaInterfaces.GetAddress(1), serverPort));
        cbrApp.SetAttribute("DataRate", StringValue("10Mbps"));
        cbrApp.SetAttribute("PacketSize", UintegerValue(4096));
        cbrApp.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        cbrApp.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        apps.Add(cbrApp.Install(wifiStaNodes.Get(0)));
    }

    if (trafficType == "Burst" || trafficType == "CBR_Burst") {
        OnOffHelper burstApp("ns3::UdpSocketFactory", InetSocketAddress(csmaInterfaces.GetAddress(1), serverPort));
        burstApp.SetAttribute("DataRate", StringValue("10Mbps"));
        burstApp.SetAttribute("PacketSize", UintegerValue(4096));
        burstApp.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1]"));
        burstApp.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=1]"));
        apps.Add(burstApp.Install(wifiStaNodes.Get(1)));
    }

    // Adicionando tráfego TCP
    uint16_t tcpPort = 8080;
    
    // Servidor TCP (fica no servidor cabeado)
    PacketSinkHelper tcpSink("ns3::TcpSocketFactory", InetSocketAddress(csmaInterfaces.GetAddress(1), tcpPort));
    apps.Add(tcpSink.Install(csmaNodes.Get(1)));

    // Cliente TCP (fica em um nó Wi-Fi)
    OnOffHelper tcpClient("ns3::TcpSocketFactory", InetSocketAddress(csmaInterfaces.GetAddress(1), tcpPort));
    tcpClient.SetAttribute("DataRate", StringValue("5Mbps"));
    tcpClient.SetAttribute("PacketSize", UintegerValue(1024));
    apps.Add(tcpClient.Install(wifiStaNodes.Get(2))); // Cliente TCP
    
    apps.Start(Seconds(2.0));
    apps.Stop(Seconds(10.0));

    // Configuração do monitor de fluxo
    FlowMonitorHelper flowMonitor;
    Ptr<FlowMonitor> monitor = flowMonitor.InstallAll();
    monitor->SerializeToXmlFile("flowmonitor-results.xml", true, true);

    // Preenche tabelas de roteamento
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Início da simulação
    Simulator::Stop(Seconds(10.0));
    Simulator::Run();

    // Coleta e exibição das estatísticas de fluxo
    uint32_t totalTxPackets = 0, totalRxPackets = 0, totalLostPackets = 0;
    double totalThroughput = 0.0, totalDelay = 0.0;

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonitor.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    for (auto &flow : stats) {
        totalTxPackets += flow.second.txPackets;
        totalRxPackets += flow.second.rxPackets;
        totalLostPackets += (flow.second.txPackets - flow.second.rxPackets);
        totalThroughput += (flow.second.rxBytes * 8.0) / (8.0 * 1000000);
        if (flow.second.rxPackets > 0)
            totalDelay += (flow.second.delaySum.GetSeconds() / flow.second.rxPackets);
    }

    // Exibe estatísticas finais
    std::cout << "Total Tx Packets: " << totalTxPackets << "\n";
    std::cout << "Total Rx Packets: " << totalRxPackets << "\n";
    std::cout << "Total Packet Loss: " << totalLostPackets << "\n";
    std::cout << "Total Throughput: " << totalThroughput << " Mbps\n";
    std::cout << "Average Delay: " << totalDelay << " s\n";

    // Finaliza a simulação
    Simulator::Destroy();
    return 0;
}
