#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/yans-wifi-phy.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/netanim-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include <netinet/in.h>
#include <iostream>
#include <vector>
#include <stdio.h>
#include <iomanip>
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"

 
using namespace ns3;
#define NUM_NODES 5
 
NS_LOG_COMPONENT_DEFINE("atv3");

class MyApp : public Application
{
public:
    MyApp ();
    virtual ~MyApp ();

    static TypeId GetTypeId (void);
    void Setup (int index,Ptr<Node> node,Ptr<Socket> sender_socket,Ptr<Socket> receiver_socket,Ipv4Address right_neighbor_address,Ipv4Address left_neighbor_address,bool is_edge);

    void StartApplication() override;
    void StopApplication() override;

    void OnAccept (Ptr<Socket> s, const Address& from);
    void OnReceive (Ptr<Socket> socket);
    void Connect (Ipv4Address neighbor_address);
    void ConnectionSucceeded(Ptr<Socket> socket);
    void ConnectionFailed(Ptr<Socket> socket);
    bool OnConnectionRequested(Ptr<Socket> socket, const Address& from);

    void SendPacket (int32_t number);

    void ChangeSpeed(Time interval);

    int index;
    Ptr<Node> node;
    Ptr<Socket> sender_socket;
    Ptr<Socket> receiver_socket;
    uint16_t receiver_port = 8080;
    bool is_running;
    bool is_edge;
    Ipv4Address right_neighbor_address;
    Ipv4Address left_neighbor_address;
};

MyApp::MyApp ()
    : sender_socket (0),
    receiver_socket (0),
    is_running (false),
    is_edge (false)
{
}

MyApp::~MyApp ()
{
    sender_socket = 0;
    receiver_socket = 0;
}

/* static */
TypeId MyApp::GetTypeId (void)
{
    static TypeId tid = TypeId ("MyApp")
    .SetParent<Application> ()
    .AddConstructor<MyApp> ()
    ;
    return tid;
}

void
MyApp::Setup (int index,Ptr<Node> node,Ptr<Socket> sender_socket,Ptr<Socket> receiver_socket,Ipv4Address right_neighbor_address,Ipv4Address left_neighbor_address,bool is_edge = false)
{
    this->index = index;
    this->node = node;
    this->sender_socket = sender_socket;
    this->receiver_socket = receiver_socket;
    this->right_neighbor_address = right_neighbor_address;
    this->left_neighbor_address = left_neighbor_address;
    this->is_edge = is_edge;
}

void
MyApp::StartApplication (void)
{
    is_running = true;

    Ptr<Socket> receiver_socket = Socket::CreateSocket (this->node, TcpSocketFactory::GetTypeId ());
    Ptr<Socket> sender_socket = Socket::CreateSocket (this->node, TcpSocketFactory::GetTypeId ());

    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), receiver_port);
    if (receiver_socket->Bind(local) == -1)
    {
      NS_FATAL_ERROR("Failed to bind socket");
    }
    receiver_socket->Listen();
    receiver_socket->SetAcceptCallback(
      MakeCallback(&MyApp::OnConnectionRequested, this),
      MakeCallback(&MyApp::OnAccept, this)
    );

    this->receiver_socket = receiver_socket;
    this->sender_socket = sender_socket;

    if(this->index == 4){
        Time interval = Seconds(5);
        Simulator::Schedule(interval, &MyApp::ChangeSpeed,this, interval);
    }

}
void
MyApp::StopApplication(void)
{
    this->is_running = false;

    if (this->receiver_socket)
    {
        this->receiver_socket->Close();
        this->receiver_socket = nullptr;
    }

    if (this->sender_socket)
    {
        this->sender_socket->Close();
        this->sender_socket = nullptr;
    }
    NS_LOG_UNCOND("Aplicação encerrada");
}

void MyApp::OnAccept(Ptr<Socket> s, const Address& from)
{
    s->SetRecvCallback(MakeCallback(&MyApp::OnReceive, this));
}

void MyApp::OnReceive(Ptr<Socket> socket)
{
    Address from;
    Ptr<Packet> packet;
    int32_t networkOrderNumber;
    int32_t receivedNumber = 0;

    Ptr<Socket> sender_socket = Socket::CreateSocket (this->node, TcpSocketFactory::GetTypeId ());
    this->sender_socket = sender_socket;

    while (packet = socket->RecvFrom(from)) {
        if (packet->GetSize() == 0) {
            break;
        }
        InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
        packet->CopyData((uint8_t *)&networkOrderNumber, sizeof(networkOrderNumber));
        receivedNumber = ntohl(networkOrderNumber);
        NS_LOG_UNCOND("velocidade recebida: " << receivedNumber);

        if (!this->is_edge) {
            if (this->right_neighbor_address == inetFrom.GetIpv4()) {
                Connect(this->left_neighbor_address);
            }
            else {
                Connect(this->right_neighbor_address);
            }
        }
        
        //setar a velocidade
        this->node->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (Vector (receivedNumber, 0.0, 0.0));
        SendPacket(receivedNumber);
    } 
}

void
MyApp::SendPacket (int32_t number)
{
    int32_t networkOrderNumber = htonl(number);
    Ptr<Packet> packet = Create<Packet>((uint8_t *)&networkOrderNumber, sizeof(networkOrderNumber));
    this->sender_socket->Send(packet);
    NS_LOG_INFO("nó "<< this->index << " manda " << number);
    sender_socket->Close();
}

void
MyApp::Connect (Ipv4Address neighbor_address)
{
    this->sender_socket->SetConnectCallback (
        MakeCallback(&MyApp::ConnectionSucceeded, this),
        MakeCallback(&MyApp::ConnectionFailed, this)
    );
    InetSocketAddress remote = InetSocketAddress(neighbor_address, this->receiver_port);
    this->sender_socket->Connect(remote);
    NS_LOG_INFO("nó "<< this->index << " conecta com " << neighbor_address);
}

// Callback para conexão bem-sucedida
void MyApp::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_INFO("Conexão bem-sucedida");
}

// Callback para falha de conexão
void MyApp::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_INFO("Falha na conexão");
}

bool MyApp::OnConnectionRequested(Ptr<Socket> socket, const Address& from) {
    NS_LOG_INFO("Conexão solicitada de: " << from);
    return true;
}

void MyApp::ChangeSpeed(Time interval) {
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " segundos: Executando Change Speed");
    Ptr<Socket> sender_socket = Socket::CreateSocket (this->node, TcpSocketFactory::GetTypeId ());
    this->sender_socket = sender_socket;

    // gerar a velocidade
    int32_t vel = rand() % 7 + 2;
    // setar a nova velocidade
    this->node->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (Vector (vel, 0.0, 0.0));
    // enviar o pacote
    Connect(this->left_neighbor_address);
    SendPacket(vel);
    Simulator::Schedule(interval, &MyApp::ChangeSpeed,this, interval);
}

int
main(int argc, char* argv[])
{
    Time::SetResolution(Time::NS);
    NodeContainer nodes;
    nodes.Create(NUM_NODES);

    /* Wifi Stuff*/
    WifiHelper wifi;
    YansWifiChannelHelper wifiChannel = ns3::YansWifiChannelHelper::Default();
    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel (wifiChannel.Create ());

    WifiMacHelper wifiMac;
    // Set it to adhoc mode
    wifiMac.SetType ("ns3::AdhocWifiMac");

    NetDeviceContainer devices;
    devices = wifi.Install (wifiPhy, wifiMac, nodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    positionAlloc->Add(Vector(5.0, 0.0, 0.0));
    positionAlloc->Add(Vector(10.0, 0.0, 0.0));
    positionAlloc->Add(Vector(15.0, 0.0, 0.0));
    positionAlloc->Add(Vector(20.0, 0.0, 0.0));
    
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
    mobility.Install (nodes);

    for (int i = 0; i < NUM_NODES; i++){
      nodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (Vector (2, 0.0, 0.0));
    }

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    for( int i = 0; i < NUM_NODES; i++ ){

        if ( i == 0 ){
            //nó inicial
            Ptr<MyApp> app = CreateObject<MyApp> ();
            app->Setup (i,nodes.Get (i),nullptr, nullptr,interfaces.GetAddress(i+1),interfaces.GetAddress(i+1),true);
            app->SetStartTime (Seconds (1.));
            app->SetStopTime (Seconds (30));
            nodes.Get (i)->AddApplication (app);
            continue;
        }
        if ( i == NUM_NODES - 1 ){
            //nó final
            Ptr<MyApp> app = CreateObject<MyApp> ();
            app->Setup (i,nodes.Get (i),nullptr, nullptr,interfaces.GetAddress(i-1),interfaces.GetAddress(i-1),true);
            app->SetStartTime (Seconds (1.));
            app->SetStopTime (Seconds (30));
            nodes.Get (i)->AddApplication (app);
            continue;
        }
        //criando aplicação
        Ptr<MyApp> app = CreateObject<MyApp> ();
        app->Setup (i,nodes.Get (i),nullptr, nullptr,interfaces.GetAddress(i+1),interfaces.GetAddress(i-1),false);
        app->SetStartTime (Seconds (1.));
        app->SetStopTime (Seconds (30));
        nodes.Get (i)->AddApplication (app);

    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    // Inicializar o NetAnim
    AnimationInterface anim("simulation.xml");
    
    Time stop_time = Seconds(30);
    Simulator::Stop(stop_time);
    Simulator::Run();
    Simulator::Destroy();
 
    return 0;
}