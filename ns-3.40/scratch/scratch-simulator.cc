/*
 * 双流BBR仿真完整代码
 * 创建两个独立的TCP流：sender0->receiver0 和 sender1->receiver1
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

std::string dir;
Time prevTime = Seconds(0);
std::vector<uint32_t> prevBytes;  // 存储每个流的字节数

// 计算每个流的吞吐量
static void
TraceThroughput(Ptr<FlowMonitor> monitor)
{
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    std::ofstream thr(dir + "/throughput.dat", std::ios::out | std::ios::app);
    Time curTime = Now();
    
    // 清空文件，重新写入表头
    static bool firstWrite = true;
    if (firstWrite) {
        thr << "Time Flow1-Thru(Mbps) Flow2-Thru(Mbps) Total-Thru(Mbps)" << std::endl;
        firstWrite = false;
    }
    
    if (stats.size() >= 2) {
        // 获取两个流的统计信息
        auto itr = stats.begin();
        FlowMonitor::FlowStats flowStats1 = itr->second;
        ++itr;
        FlowMonitor::FlowStats flowStats2 = itr->second;
        
        // 计算瞬时吞吐量
        double throughput1 = 8.0 * (flowStats1.txBytes - prevBytes[0]) / 
                           (1000 * 1000 * (curTime.GetSeconds() - prevTime.GetSeconds()));
        double throughput2 = 8.0 * (flowStats2.txBytes - prevBytes[1]) / 
                           (1000 * 1000 * (curTime.GetSeconds() - prevTime.GetSeconds()));
        double totalThroughput = throughput1 + throughput2;
        
        // 写入文件
        thr << curTime << " " << throughput1 << " " << throughput2 << " " << totalThroughput << std::endl;
        
        // 更新前一时刻的字节数
        prevBytes[0] = flowStats1.txBytes;
        prevBytes[1] = flowStats2.txBytes;
    }
    
    prevTime = curTime;
    Simulator::Schedule(Seconds(0.2), &TraceThroughput, monitor);
}

// 检查队列大小
void
CheckQueueSize(Ptr<QueueDisc> qd)
{
    uint32_t qsize = qd->GetCurrentSize().GetValue();
    Simulator::Schedule(Seconds(0.2), &CheckQueueSize, qd);
    std::ofstream q(dir + "/queueSize.dat", std::ios::out | std::ios::app);
    q << Simulator::Now().GetSeconds() << " " << qsize << std::endl;
    q.close();
}

// 拥塞窗口跟踪
static void
CwndTracer(Ptr<OutputStreamWrapper> stream, uint32_t oldval, uint32_t newval)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " " << newval / 1448.0 << std::endl;
}

// 修改TraceCwnd以支持多个流
void
TraceCwnd(uint32_t nodeId, uint32_t socketId, const std::string& flowName)
{
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream(dir + "/cwnd-" + flowName + ".dat");
    // 写入表头
    *stream->GetStream() << "Time Cwnd(Segments)" << std::endl;
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(nodeId) +
                                      "/$ns3::TcpL4Protocol/SocketList/" +
                                      std::to_string(socketId) + "/CongestionWindow",
                                  MakeBoundCallback(&CwndTracer, stream));
}

int
main(int argc, char* argv[])
{
    // 设置输出目录
    time_t rawtime;
    struct tm* timeinfo;
    char buffer[80];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%d-%m-%Y-%I-%M-%S", timeinfo);
    std::string currentTime(buffer);
    
    // 参数设置
    std::string tcpTypeId = "TcpBbr";
    std::string queueDisc = "FifoQueueDisc";
    uint32_t delAckCount = 2;
    bool bql = true;
    bool enablePcap = false;
    Time stopTime = Seconds(100);
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpTypeId", "Transport protocol to use: TcpNewReno, TcpBbr", tcpTypeId);
    cmd.AddValue("delAckCount", "Delayed ACK count", delAckCount);
    cmd.AddValue("enablePcap", "Enable/Disable pcap file generation", enablePcap);
    cmd.AddValue("stopTime", "Stop time for applications", stopTime);
    cmd.Parse(argc, argv);
    
    queueDisc = std::string("ns3::") + queueDisc;
    
    // TCP配置
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::" + tcpTypeId));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue(QueueSize("1p")));
    Config::SetDefault(queueDisc + "::MaxSize", QueueSizeValue(QueueSize("100p")));
    
    // 创建节点
    NodeContainer sender;
    NodeContainer receiver;
    NodeContainer routers;
    sender.Create(2);
    receiver.Create(2);
    routers.Create(3);
    
    // 创建链路
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("1000Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("5ms"));
    
    PointToPointHelper edgeLink;
    edgeLink.SetDeviceAttribute("DataRate", StringValue("1000Mbps"));
    edgeLink.SetChannelAttribute("Delay", StringValue("5ms"));
    
    // 连接节点
    NetDeviceContainer senderEdge1 = edgeLink.Install(sender.Get(0), routers.Get(0));
    NetDeviceContainer senderEdge2 = edgeLink.Install(sender.Get(1), routers.Get(0));
    NetDeviceContainer r1r2 = bottleneckLink.Install(routers.Get(0), routers.Get(1));
    NetDeviceContainer r2r3 = bottleneckLink.Install(routers.Get(1), routers.Get(2));
    NetDeviceContainer receiverEdge1 = edgeLink.Install(routers.Get(2), receiver.Get(0));
    NetDeviceContainer receiverEdge2 = edgeLink.Install(routers.Get(2), receiver.Get(1));
    
    // 安装协议栈
    InternetStackHelper internet;
    internet.Install(sender);
    internet.Install(receiver);
    internet.Install(routers);
    
    // 配置队列管理
    TrafficControlHelper tch;
    tch.SetRootQueueDisc(queueDisc);
    
    if (bql) {
        tch.SetQueueLimits("ns3::DynamicQueueLimits", "HoldTime", StringValue("1000ms"));
    }
    
    tch.Install(senderEdge1);
    tch.Install(senderEdge2);
    tch.Install(receiverEdge1);
    tch.Install(receiverEdge2);
    
    // 分配IP地址
    Ipv4AddressHelper ipv4;
    
    // 路由器之间的链路
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i1i2 = ipv4.Assign(r1r2);
    
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i2i3 = ipv4.Assign(r2r3);
    
    // 发送端到路由器
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer is1 = ipv4.Assign(senderEdge1);
    
    ipv4.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer is2 = ipv4.Assign(senderEdge2);
    
    // 路由器到接收端
    ipv4.SetBase("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer ir1 = ipv4.Assign(receiverEdge1);
    
    ipv4.SetBase("10.1.6.0", "255.255.255.0");
    Ipv4InterfaceContainer ir2 = ipv4.Assign(receiverEdge2);
    
    // 配置路由
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // 创建两个独立的TCP连接
    uint16_t port1 = 50001;
    uint16_t port2 = 50002;
    
    // 初始化prevBytes向量
    prevBytes.resize(2, 0);
    
    // ========== 第一个TCP流：sender0 -> receiver0 ==========
    BulkSendHelper source1("ns3::TcpSocketFactory", 
                          InetSocketAddress(ir1.GetAddress(1), port1));
    source1.SetAttribute("MaxBytes", UintegerValue(0));
    source1.SetAttribute("SendSize", UintegerValue(1448));
    ApplicationContainer sourceApps1 = source1.Install(sender.Get(0));
    sourceApps1.Start(Seconds(0.1));
    sourceApps1.Stop(stopTime);
    
    PacketSinkHelper sink1("ns3::TcpSocketFactory", 
                          InetSocketAddress(Ipv4Address::GetAny(), port1));
    ApplicationContainer sinkApps1 = sink1.Install(receiver.Get(0));
    sinkApps1.Start(Seconds(0.0));
    sinkApps1.Stop(stopTime);
    
    // ========== 第二个TCP流：sender1 -> receiver1 ==========
    BulkSendHelper source2("ns3::TcpSocketFactory", 
                          InetSocketAddress(ir2.GetAddress(1), port2));
    source2.SetAttribute("MaxBytes", UintegerValue(0));
    source2.SetAttribute("SendSize", UintegerValue(1448));
    ApplicationContainer sourceApps2 = source2.Install(sender.Get(1));
    sourceApps2.Start(Seconds(0.2));  // 错开0.1秒启动，避免完全同步
    sourceApps2.Stop(stopTime);
    
    PacketSinkHelper sink2("ns3::TcpSocketFactory", 
                          InetSocketAddress(Ipv4Address::GetAny(), port2));
    ApplicationContainer sinkApps2 = sink2.Install(receiver.Get(1));
    sinkApps2.Start(Seconds(0.0));
    sinkApps2.Stop(stopTime);
    
    // 创建输出目录
    dir = "bbr-two-flow-results/" + currentTime + "/";
    std::string dirToSave = "mkdir -p " + dir;
    if (system(dirToSave.c_str()) == -1) {
        exit(1);
    }
    
    // 跟踪拥塞窗口
    Simulator::Schedule(Seconds(0.1) + MilliSeconds(1), &TraceCwnd, 0, 0, "flow1");
    Simulator::Schedule(Seconds(0.2) + MilliSeconds(1), &TraceCwnd, 1, 0, "flow2");
    
    // 跟踪队列大小
    tch.Uninstall(routers.Get(0)->GetDevice(2));
    QueueDiscContainer qd;
    qd = tch.Install(routers.Get(0)->GetDevice(2));
    Simulator::ScheduleNow(&CheckQueueSize, qd.Get(0));
    
    // 生成PCAP文件
    if (enablePcap) {
        if (system((dirToSave + "/pcap/").c_str()) == -1) {
            exit(1);
        }
        edgeLink.EnablePcapAll(dir + "/pcap/edge");
        bottleneckLink.EnablePcapAll(dir + "/pcap/bottleneck");
    }
    
    // 监控流
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    Simulator::Schedule(Seconds(0.2), &TraceThroughput, monitor);
    
    // 运行仿真
    std::cout << "Starting simulation with 2 flows..." << std::endl;
    std::cout << "Flow1: sender0 (10.1.3.1) -> receiver0 (10.1.5.2):" << port1 << std::endl;
    std::cout << "Flow2: sender1 (10.1.4.1) -> receiver1 (10.1.6.2):" << port2 << std::endl;
    std::cout << "Results saved to: " << dir << std::endl;
    
    Simulator::Stop(stopTime + TimeStep(1));
    Simulator::Run();
    Simulator::Destroy();
    
    return 0;
}