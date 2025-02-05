#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

// Define um componente de log para depuração
NS_LOG_COMPONENT_DEFINE("MixedTopologySimulation");

int main(int argc, char *argv[]) {
    // Definição de parâmetros padrão e possibilidade de alteração via linha de comando
    bool enableMobility = true;  // Define se os nós sem fio terão mobilidade
    uint32_t numWirelessNodes = 5; // Número de nós Wi-Fi
    double simTime = 10.0; // Tempo total da simulação (em segundos)
    double mobilitySpeed = 1.0; // Velocidade dos nós móveis (m/s)

    // Permite modificar os parâmetros via linha de comando
    CommandLine cmd;
    cmd.AddValue("enableMobility", "Habilita mobilidade nos nós sem fio", enableMobility);
    cmd.AddValue("numWirelessNodes", "Número de nós sem fio", numWirelessNodes);
    cmd.AddValue("mobilitySpeed", "Velocidade dos nós móveis (m/s)", mobilitySpeed); 
    cmd.Parse(argc, argv);

    std::cout << "\nIniciando a Simulação...\n" << std::endl;

    // Criação de containers de nós
    NodeContainer nodes, apNode, wirelessNodes;
    nodes.Create(2);  // Dois nós cabeados
    apNode.Create(1);  // Um nó Access Point (AP)
    wirelessNodes.Create(numWirelessNodes); // Nós sem fio

    // Configuração da mobilidade dos nós sem fio
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
        "MinX", DoubleValue(0.0),
        "MinY", DoubleValue(0.0),
        "DeltaX", DoubleValue(5.0),
        "DeltaY", DoubleValue(5.0),
        "GridWidth", UintegerValue(3),
        "LayoutType", StringValue("RowFirst"));
        
    if (enableMobility) {
        // Define o modelo de mobilidade para os nós sem fio
        mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
        mobility.Install(wirelessNodes);
        for (uint32_t i = 0; i < wirelessNodes.GetN(); i++) {
            Ptr<ConstantVelocityMobilityModel> mob = wirelessNodes.Get(i)->GetObject<ConstantVelocityMobilityModel>();
            mob->SetVelocity(Vector(mobilitySpeed, 0, 0)); // Movendo-se na direção X com a velocidade definida
        }
    } else {
        // Caso a mobilidade esteja desativada, os nós permanecem estáticos
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(wirelessNodes);
    }

    // O AP não se move, então usa o modelo de posição constante
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);

    // Configuração da rede cabeada (P2P)
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer devices = p2p.Install(nodes);

    // Configuração da rede Wi-Fi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n); // Define padrão 802.11n para a rede sem fio
    WifiMacHelper mac;
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(wifiChannel.Create());

    // Configuração do SSID e MAC dos dispositivos Wi-Fi
    Ssid ssid = Ssid("ns3-wifi");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer wifiDevices = wifi.Install(wifiPhy, mac, wirelessNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(wifiPhy, mac, apNode);

    // Instala a pilha de protocolos de Internet nos nós
    InternetStackHelper stack;
    stack.Install(nodes);
    stack.Install(apNode);
    stack.Install(wirelessNodes);

    // Configuração de endereçamento IP para a rede cabeada
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // Configuração de endereçamento IP para a rede Wi-Fi
    address.SetBase("192.168.0.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);
    Ipv4InterfaceContainer wifiInterfaces = address.Assign(wifiDevices);

    // Configuração de um servidor UDP na rede cabeada
    uint16_t port = 9;
    UdpServerHelper udpServer(port);
    ApplicationContainer serverApp = udpServer.Install(nodes.Get(1));
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(simTime));

    // Configuração de um cliente UDP na rede cabeada
    UdpClientHelper udpClient(interfaces.GetAddress(1), port);
    udpClient.SetAttribute("MaxPackets", UintegerValue(5000));
    udpClient.SetAttribute("Interval", TimeValue(MicroSeconds(100)));
    udpClient.SetAttribute("PacketSize", UintegerValue(2048));
    ApplicationContainer clientApp = udpClient.Install(nodes.Get(0));
    clientApp.Start(Seconds(2.0));
    clientApp.Stop(Seconds(simTime));

    // Configuração de um servidor UDP na rede Wi-Fi
    UdpServerHelper wifiUdpServer(10);
    ApplicationContainer wifiServerApp = wifiUdpServer.Install(wirelessNodes.Get(1));
    wifiServerApp.Start(Seconds(1.0));
    wifiServerApp.Stop(Seconds(simTime));

    // Configuração de um cliente UDP na rede Wi-Fi
    UdpClientHelper wifiUdpClient(wifiInterfaces.GetAddress(1), 10);
    wifiUdpClient.SetAttribute("MaxPackets", UintegerValue(1000));
    wifiUdpClient.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
    wifiUdpClient.SetAttribute("PacketSize", UintegerValue(1024));
    ApplicationContainer wifiClientApp = wifiUdpClient.Install(wirelessNodes.Get(0));
    wifiClientApp.Start(Seconds(2.0));
    wifiClientApp.Stop(Seconds(simTime));

    // Configuração de uma aplicação TCP entre Wi-Fi e cabeada
    uint16_t tcpPort = 8080;
    PacketSinkHelper tcpSink("ns3::TcpSocketFactory", InetSocketAddress(interfaces.GetAddress(1), tcpPort));
    ApplicationContainer sinkApp = tcpSink.Install(nodes.Get(1));
    sinkApp.Start(Seconds(1.0));
    sinkApp.Stop(Seconds(simTime));

    OnOffHelper tcpClient("ns3::TcpSocketFactory", InetSocketAddress(interfaces.GetAddress(1), tcpPort));
    tcpClient.SetAttribute("DataRate", StringValue("5Mbps"));
    tcpClient.SetAttribute("PacketSize", UintegerValue(1024));
    ApplicationContainer tcpApp = tcpClient.Install(wirelessNodes.Get(0));
    tcpApp.Start(Seconds(2.0));
    tcpApp.Stop(Seconds(simTime));

    // Ativa rastreamento de pacotes
    p2p.EnablePcapAll("topologia");
    wifiPhy.EnablePcap("topologia", apDevice.Get(0));
    AsciiTraceHelper ascii;
    p2p.EnableAsciiAll(ascii.CreateFileStream("topologia.tr"));

    // Configuração do FlowMonitor para estatísticas
    FlowMonitorHelper flowMonitor;
    Ptr<FlowMonitor> monitor = flowMonitor.InstallAll();
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Coleta e exibe estatísticas do fluxo de tráfego
    monitor->CheckForLostPackets();
    monitor->SerializeToXmlFile("flowmonitor.xml", true, true);

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonitor.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "Número de Fluxos Monitorados: " << stats.size() << std::endl;
    if (stats.empty()) {
        std::cout << "⚠️ Nenhum fluxo foi detectado! Verifica a configuração das aplicações.\n";
        monitor->SerializeToXmlFile("flowmonitor.xml", true, true);
    }

    std::cout << "\nEstatísticas do FlowMonitor:\n";
    for (auto iter = stats.begin(); iter != stats.end(); ++iter) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter->first);
        std::cout << "Fluxo " << iter->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
        std::cout << "  Pacotes TX: " << iter->second.txPackets << "\n";
        std::cout << "  Pacotes RX: " << iter->second.rxPackets << "\n";

        if (iter->second.rxPackets > 0) {
            std::cout << "  Vazão: " << (iter->second.rxBytes * 8.0 / simTime / 1e6) << " Mbps\n";
            std::cout << "  Atraso médio: " << (iter->second.delaySum.GetSeconds() / iter->second.rxPackets) << " s\n";
            std::cout << "  Perda de Pacotes: " << iter->second.lostPackets << "\n\n";
        } else {
            std::cout << "  Vazão: 0 Mbps\n";
            std::cout << "  Atraso médio: N/A (nenhum pacote recebido)\n";
        }
    }

    
    Simulator::Destroy();
    return 0;
}
