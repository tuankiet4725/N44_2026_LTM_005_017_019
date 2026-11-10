/*
 * Kịch bản mô phỏng: Cơ chế QoS ưu tiên dữ liệu khẩn cấp
 *
 *   E0..En-1 (cảm biến khẩn cấp, DSCP EF) ─┐
 *   C0..C1   (video giám sát, AF41)       ─┼─ R0 ══(nút cổ chai 2 Mb/s, 10 ms)══ R1 ── Sink
 *   B0 (UDP nền), B1 (TCP bulk), BE        ─┘   ^ queue disc được so sánh đặt ở đây
 *
 * So sánh 3 cấu hình tại giao diện cổ chai của R0:
 *   --scheme=fifo : FIFO DropTail (TRƯỚC - baseline)
 *   --scheme=prio : Strict Priority 3 băng theo DSCP (baseline QoS cổ điển)
 *   --scheme=epq  : Emergency-aware Priority Queue (SAU - đề xuất)
 *
 * Ví dụ: ./ns3 run "emergency-qos --scheme=epq --load=1.2 --run=1 --out=/tmp/r.csv"
 */
#include "qos-components.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EmergencyQosScenario");

static const uint16_t E_PORT = 10000;
static const uint16_t C_PORT = 20000;
static const uint16_t BE_PORT = 30000;
static const char* CLASS_NAME[3] = {"emergency", "critical", "best_effort"};

struct ClassStats
{
    uint64_t tx = 0;
    uint64_t rx = 0;
    uint64_t rxBytes = 0;
    double delaySum = 0;
    double jitterSum = 0;
    uint64_t jitterSamples = 0;
    std::vector<uint64_t> hist;
};

static int
PortToClass(uint16_t port)
{
    if (port >= BE_PORT)
        return TC_BEST_EFFORT;
    if (port >= C_PORT)
        return TC_CRITICAL;
    return TC_EMERGENCY;
}

static double
Percentile(const std::vector<uint64_t>& hist, double binWidth, double p)
{
    uint64_t total = 0;
    for (auto c : hist)
        total += c;
    if (total == 0)
        return 0;
    uint64_t target = static_cast<uint64_t>(p * total);
    uint64_t cum = 0;
    for (size_t b = 0; b < hist.size(); ++b)
    {
        cum += hist[b];
        if (cum >= target)
            return (b + 0.5) * binWidth;
    }
    return hist.size() * binWidth;
}

static TrafficControlHelper
BuildQdisc(const std::string& scheme,
           uint32_t bufferPkts,
           const std::string& eRate,
           uint32_t eBurst,
           bool pushOut)
{
    TrafficControlHelper tch;
    uint32_t eBuf = bufferPkts * 3 / 10; // 30% bộ đệm cho lớp khẩn cấp
    if (scheme == "fifo")
    {
        tch.SetRootQueueDisc("ns3::FifoQueueDisc",
                             "MaxSize",
                             QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, bufferPkts)));
    }
    else if (scheme == "prio")
    {
        uint16_t h = tch.SetRootQueueDisc("ns3::PrioQueueDisc");
        TrafficControlHelper::ClassIdList cls = tch.AddQueueDiscClasses(h, 3, "ns3::QueueDiscClass");
        uint32_t cBuf = (bufferPkts - eBuf) / 2;
        uint32_t bBuf = bufferPkts - eBuf - cBuf;
        tch.AddChildQueueDisc(h, cls[0], "ns3::FifoQueueDisc",
                              "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, eBuf)));
        tch.AddChildQueueDisc(h, cls[1], "ns3::FifoQueueDisc",
                              "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, cBuf)));
        tch.AddChildQueueDisc(h, cls[2], "ns3::FifoQueueDisc",
                              "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, bBuf)));
        tch.AddPacketFilter(h, "ns3::DscpPacketFilter");
    }
    else if (scheme == "epq")
    {
        tch.SetRootQueueDisc("ns3::EmergencyQueueDisc",
                             "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, bufferPkts)),
                             "EmergencyRate", DataRateValue(DataRate(eRate)),
                             "EmergencyBurst", UintegerValue(eBurst),
                             "EmergencyQueueLimit", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, eBuf)),
                             "PushOut", BooleanValue(pushOut));
    }
    else
    {
        NS_FATAL_ERROR("Unknown scheme: " << scheme << " (use fifo|prio|epq)");
    }
    return tch;
}

int
main(int argc, char* argv[])
{
    std::string scheme = "epq";
    std::string experiment = "A";
    double load = 1.2;          // tải đề nghị / dung lượng cổ chai
    double eScale = 1.0;        // hệ số nhân tốc độ nguồn khẩn cấp (kịch bản "bão" sự kiện)
    uint32_t nEmergency = 10;
    double simTime = 60.0;
    uint32_t run = 1;
    uint32_t bufferPkts = 100;
    std::string bottleneckRate = "2Mbps";
    std::string eRate = "600kbps";
    uint32_t eBurst = 15000;
    bool pushOut = true;
    bool enableTcp = true;
    std::string out = "emergency-qos-results.csv";

    CommandLine cmd(__FILE__);
    cmd.AddValue("scheme", "fifo | prio | epq", scheme);
    cmd.AddValue("experiment", "Nhãn thí nghiệm ghi vào CSV (A: quét tải, B: bão khẩn cấp)", experiment);
    cmd.AddValue("load", "Tải đề nghị chuẩn hoá theo dung lượng cổ chai", load);
    cmd.AddValue("eScale", "Hệ số nhân tốc độ nguồn khẩn cấp", eScale);
    cmd.AddValue("nEmergency", "Số nút cảm biến khẩn cấp", nEmergency);
    cmd.AddValue("simTime", "Thời gian mô phỏng (s)", simTime);
    cmd.AddValue("run", "Chỉ số RngRun (lặp lại độc lập)", run);
    cmd.AddValue("buffer", "Kích thước bộ đệm cổ chai (gói)", bufferPkts);
    cmd.AddValue("bottleneckRate", "Dung lượng cổ chai", bottleneckRate);
    cmd.AddValue("eRate", "EPQ: tốc độ token khẩn cấp", eRate);
    cmd.AddValue("eBurst", "EPQ: kích thước xô token (byte)", eBurst);
    cmd.AddValue("pushOut", "EPQ: bật push-out", pushOut);
    cmd.AddValue("tcp", "Bật luồng TCP bulk best-effort", enableTcp);
    cmd.AddValue("out", "File CSV kết quả (ghi nối)", out);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(20261029);
    RngSeedManager::SetRun(run);

    const uint32_t nCritical = 2;
    const uint32_t nBe = 2;
    const double eOnMean = 0.5, eOffMean = 1.5, ePeakBps = 64000.0 * eScale;
    const double cRateBps = 500000.0;
    const double capBps = DataRate(bottleneckRate).GetBitRate();
    const double eAvgBps = nEmergency * ePeakBps * eOnMean / (eOnMean + eOffMean);
    const double beUdpBps = std::max(50000.0, load * capBps - eAvgBps - nCritical * cRateBps);

    // ---------------- Topology ----------------
    NodeContainer eNodes, cNodes, beNodes, routers, sinkNode;
    eNodes.Create(nEmergency);
    cNodes.Create(nCritical);
    beNodes.Create(nBe);
    routers.Create(2);
    sinkNode.Create(1);

    InternetStackHelper stack;
    stack.InstallAll();

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    access.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckRate));
    bottleneck.SetChannelAttribute("Delay", StringValue("10ms"));
    // Hàng đợi của NetDevice = 1 gói để toàn bộ việc đệm/lập lịch nằm ở queue disc
    bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

    Ipv4AddressHelper addr;
    addr.SetBase("10.1.0.0", "255.255.255.0");

    NodeContainer sources;
    sources.Add(eNodes);
    sources.Add(cNodes);
    sources.Add(beNodes);
    for (uint32_t i = 0; i < sources.GetN(); ++i)
    {
        NetDeviceContainer d = access.Install(sources.Get(i), routers.Get(0));
        addr.Assign(d);
        addr.NewNetwork();
    }

    NetDeviceContainer bnDev = bottleneck.Install(routers.Get(0), routers.Get(1));
    TrafficControlHelper tch = BuildQdisc(scheme, bufferPkts, eRate, eBurst, pushOut);
    QueueDiscContainer qdiscs = tch.Install(bnDev.Get(0)); // phải cài TRƯỚC khi Assign
    addr.Assign(bnDev);
    addr.NewNetwork();

    NetDeviceContainer sinkDev = access.Install(routers.Get(1), sinkNode.Get(0));
    Ipv4InterfaceContainer sinkIf = addr.Assign(sinkDev);
    Ipv4Address sinkAddr = sinkIf.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // ---------------- Applications ----------------
    const double appStart = 1.0;
    auto addSink = [&](uint16_t port, bool tcp) {
        PacketSinkHelper ps(tcp ? "ns3::TcpSocketFactory" : "ns3::UdpSocketFactory",
                            InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer a = ps.Install(sinkNode.Get(0));
        a.Start(Seconds(0.0));
    };

    // (1) Dữ liệu khẩn cấp: bùng nổ ON/OFF, gói nhỏ, DSCP EF (0xb8)
    for (uint32_t i = 0; i < nEmergency; ++i)
    {
        uint16_t port = E_PORT + i;
        addSink(port, false);
        OnOffHelper oo("ns3::UdpSocketFactory", InetSocketAddress(sinkAddr, port));
        oo.SetAttribute("DataRate", DataRateValue(DataRate(static_cast<uint64_t>(ePeakBps))));
        oo.SetAttribute("PacketSize", UintegerValue(200));
        oo.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.5]"));
        oo.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.5]"));
        oo.SetAttribute("Tos", UintegerValue(0xb8)); // ns-3.40+; xem README nếu dùng bản cũ
        ApplicationContainer a = oo.Install(eNodes.Get(i));
        a.Start(Seconds(appStart + 0.013 * i));
        a.Stop(Seconds(simTime));
    }

    // (2) Video giám sát (critical): CBR, DSCP AF41 (0x88)
    for (uint32_t i = 0; i < nCritical; ++i)
    {
        uint16_t port = C_PORT + i;
        addSink(port, false);
        OnOffHelper oo("ns3::UdpSocketFactory", InetSocketAddress(sinkAddr, port));
        oo.SetConstantRate(DataRate(static_cast<uint64_t>(cRateBps)), 1000);
        oo.SetAttribute("Tos", UintegerValue(0x88));
        ApplicationContainer a = oo.Install(cNodes.Get(i));
        a.Start(Seconds(appStart + 0.007 * i));
        a.Stop(Seconds(simTime));
    }

    // (3) Best-effort: UDP nền (điều khiển tải) + TCP bulk
    {
        uint16_t port = BE_PORT;
        addSink(port, false);
        OnOffHelper oo("ns3::UdpSocketFactory", InetSocketAddress(sinkAddr, port));
        oo.SetConstantRate(DataRate(static_cast<uint64_t>(beUdpBps)), 1000);
        oo.SetAttribute("Tos", UintegerValue(0x00));
        ApplicationContainer a = oo.Install(beNodes.Get(0));
        a.Start(Seconds(appStart));
        a.Stop(Seconds(simTime));
    }
    if (enableTcp)
    {
        uint16_t port = BE_PORT + 1;
        addSink(port, true);
        BulkSendHelper bs("ns3::TcpSocketFactory", InetSocketAddress(sinkAddr, port));
        bs.SetAttribute("MaxBytes", UintegerValue(0));
        bs.SetAttribute("Tos", UintegerValue(0x00));
        ApplicationContainer a = bs.Install(beNodes.Get(1));
        a.Start(Seconds(appStart));
        a.Stop(Seconds(simTime));
    }

    // ---------------- Measurement ----------------
    const double binWidth = 0.001;
    FlowMonitorHelper fmh;
    Ptr<FlowMonitor> mon = fmh.InstallAll();
    mon->SetAttribute("DelayBinWidth", DoubleValue(binWidth));
    mon->SetAttribute("JitterBinWidth", DoubleValue(binWidth));

    Simulator::Stop(Seconds(simTime + 2.0)); // 2 s để xả hàng đợi
    Simulator::Run();

    mon->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> clf = DynamicCast<Ipv4FlowClassifier>(fmh.GetClassifier());
    std::array<ClassStats, 3> cs;
    for (const auto& [id, st] : mon->GetFlowStats())
    {
        Ipv4FlowClassifier::FiveTuple t = clf->FindFlow(id);
        if (t.destinationAddress != sinkAddr)
            continue; // bỏ luồng ACK chiều ngược
        ClassStats& c = cs[PortToClass(t.destinationPort)];
        c.tx += st.txPackets;
        c.rx += st.rxPackets;
        c.rxBytes += st.rxBytes;
        c.delaySum += st.delaySum.GetSeconds();
        c.jitterSum += st.jitterSum.GetSeconds();
        c.jitterSamples += st.rxPackets > 1 ? st.rxPackets - 1 : 0;
        const Histogram& h = st.delayHistogram;
        if (c.hist.size() < h.GetNBins())
            c.hist.resize(h.GetNBins(), 0);
        for (uint32_t b = 0; b < h.GetNBins(); ++b)
            c.hist[b] += h.GetBinCount(b);
    }

    uint32_t pushOuts = 0;
    if (Ptr<EmergencyQueueDisc> epq = DynamicCast<EmergencyQueueDisc>(qdiscs.Get(0)))
    {
        pushOuts = epq->GetPushOutCount();
        std::cout << "EPQ strict=" << epq->GetStrictServedCount()
                  << " demoted=" << epq->GetDemotedServedCount() << " pushout=" << pushOuts
                  << std::endl;
    }
    std::cout << qdiscs.Get(0)->GetStats() << std::endl;

    bool newFile = !std::ifstream(out).good();
    std::ofstream f(out, std::ios::app);
    if (newFile)
    {
        f << "experiment,scheme,load,e_scale,n_emergency,run,class,tx_pkts,rx_pkts,loss_pct,"
             "throughput_kbps,mean_delay_ms,p95_delay_ms,jitter_ms,pushouts\n";
    }
    const double dur = simTime - appStart;
    for (int k = 0; k < 3; ++k)
    {
        const ClassStats& c = cs[k];
        double loss = c.tx ? 100.0 * (double)(c.tx - c.rx) / c.tx : 0;
        double thr = c.rxBytes * 8.0 / dur / 1000.0;
        double md = c.rx ? 1000.0 * c.delaySum / c.rx : 0;
        double p95 = 1000.0 * Percentile(c.hist, binWidth, 0.95);
        double jit = c.jitterSamples ? 1000.0 * c.jitterSum / c.jitterSamples : 0;
        f << experiment << "," << scheme << "," << load << "," << eScale << "," << nEmergency << ","
          << run << "," << CLASS_NAME[k] << "," << c.tx << "," << c.rx << "," << std::fixed
          << std::setprecision(4) << loss << "," << thr << "," << md << "," << p95 << "," << jit
          << "," << pushOuts << "\n";
        f.unsetf(std::ios::fixed);
        std::cout << std::setw(12) << CLASS_NAME[k] << " loss=" << loss << "% thr=" << thr
                  << "kbps delay=" << md << "ms p95=" << p95 << "ms jitter=" << jit << "ms\n";
    }

    Simulator::Destroy();
    return 0;
}
