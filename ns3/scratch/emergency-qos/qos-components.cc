#include "qos-components.h"

#include "ns3/boolean.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("EmergencyQosComponents");

// Lay DSCP truc tiep tu header IPv4 (tuong thich moi phien ban ns-3)
static bool
GetDscp(Ptr<const QueueDiscItem> item, uint8_t& dscp)
{
    Ptr<const Ipv4QueueDiscItem> ip = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (!ip)
    {
        return false;
    }
    dscp = ip->GetHeader().GetTos() >> 2;
    return true;
}

uint8_t
DscpToTrafficClass(uint8_t dscp)
{
    switch (dscp)
    {
    case 46: // EF  - cảnh báo khẩn cấp
    case 48: // CS6
        return TC_EMERGENCY;
    case 32: // CS4
    case 34: // AF41
    case 36: // AF42
    case 38: // AF43
    case 40: // CS5
        return TC_CRITICAL;
    default:
        return TC_BEST_EFFORT;
    }
}

/* ======================= DscpPacketFilter ======================= */

NS_OBJECT_ENSURE_REGISTERED(DscpPacketFilter);

TypeId
DscpPacketFilter::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DscpPacketFilter")
                            .SetParent<PacketFilter>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<DscpPacketFilter>();
    return tid;
}

DscpPacketFilter::DscpPacketFilter() = default;
DscpPacketFilter::~DscpPacketFilter() = default;

bool
DscpPacketFilter::CheckProtocol(Ptr<QueueDiscItem> item) const
{
    return DynamicCast<Ipv4QueueDiscItem>(item) != nullptr;
}

int32_t
DscpPacketFilter::DoClassify(Ptr<QueueDiscItem> item) const
{
    uint8_t dscp = 0;
    if (!GetDscp(item, dscp))
    {
        return PacketFilter::PF_NO_MATCH;
    }
    return DscpToTrafficClass(dscp);
}

/* ======================= EmergencyQueueDisc ======================= */

NS_OBJECT_ENSURE_REGISTERED(EmergencyQueueDisc);

TypeId
EmergencyQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::EmergencyQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<EmergencyQueueDisc>()
            .AddAttribute("MaxSize",
                          "Tổng dung lượng bộ đệm của queue disc",
                          QueueSizeValue(QueueSize("100p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("EmergencyRate",
                          "Tốc độ token: phần băng thông tối đa được phục vụ ưu tiên tuyệt đối",
                          DataRateValue(DataRate("600kb/s")),
                          MakeDataRateAccessor(&EmergencyQueueDisc::m_emergencyRate),
                          MakeDataRateChecker())
            .AddAttribute("EmergencyBurst",
                          "Kích thước xô token (byte)",
                          UintegerValue(15000),
                          MakeUintegerAccessor(&EmergencyQueueDisc::m_emergencyBurst),
                          MakeUintegerChecker<uint32_t>(1500))
            .AddAttribute("EmergencyQueueLimit",
                          "Giới hạn riêng của hàng đợi khẩn cấp",
                          QueueSizeValue(QueueSize("30p")),
                          MakeQueueSizeAccessor(&EmergencyQueueDisc::m_emergencyLimit),
                          MakeQueueSizeChecker())
            .AddAttribute("PushOut",
                          "Push-out theo thứ bậc: khi bộ đệm đầy, gói ưu tiên cao đẩy bỏ gói ưu tiên thấp hơn",
                          BooleanValue(true),
                          MakeBooleanAccessor(&EmergencyQueueDisc::m_pushOut),
                          MakeBooleanChecker())
            .AddAttribute("EmergencyQuantum",
                          "Quantum DRR (byte) cho gói khẩn cấp bị hạ cấp",
                          UintegerValue(1500),
                          MakeUintegerAccessor(&EmergencyQueueDisc::m_quantumE),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("CriticalQuantum",
                          "Quantum DRR (byte) cho lớp critical",
                          UintegerValue(4500),
                          MakeUintegerAccessor(&EmergencyQueueDisc::m_quantumC),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("BestEffortQuantum",
                          "Quantum DRR (byte) cho lớp best-effort",
                          UintegerValue(1500),
                          MakeUintegerAccessor(&EmergencyQueueDisc::m_quantumBE),
                          MakeUintegerChecker<uint32_t>(1));
    return tid;
}

EmergencyQueueDisc::EmergencyQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS),
      m_tokens(0),
      m_deficit{0, 0, 0},
      m_rr(TC_EMERGENCY),
      m_quantumGranted(false),
      m_pushOuts(0),
      m_strictServed(0),
      m_demotedServed(0)
{
    NS_LOG_FUNCTION(this);
}

EmergencyQueueDisc::~EmergencyQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

uint32_t
EmergencyQueueDisc::GetPushOutCount() const
{
    return m_pushOuts;
}

uint32_t
EmergencyQueueDisc::GetStrictServedCount() const
{
    return m_strictServed;
}

uint32_t
EmergencyQueueDisc::GetDemotedServedCount() const
{
    return m_demotedServed;
}

uint8_t
EmergencyQueueDisc::ClassOf(Ptr<const QueueDiscItem> item) const
{
    uint8_t dscp = 0;
    if (GetDscp(item, dscp))
    {
        return DscpToTrafficClass(dscp);
    }
    return TC_BEST_EFFORT;
}

void
EmergencyQueueDisc::RefillTokens()
{
    Time now = Simulator::Now();
    double add = (now - m_lastRefill).GetSeconds() * m_emergencyRate.GetBitRate() / 8.0;
    m_tokens = std::min<double>(m_emergencyBurst, m_tokens + add);
    m_lastRefill = now;
}

void
EmergencyQueueDisc::NextClass()
{
    m_rr = (m_rr + 1) % 3;
    m_quantumGranted = false;
}

bool
EmergencyQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);
    uint8_t c = ClassOf(item);
    Ptr<InternalQueue> q = GetInternalQueue(c);

    // 1) Giới hạn riêng của từng lớp
    if (q->GetCurrentSize() + item > q->GetMaxSize())
    {
        DropBeforeEnqueue(item, LIMIT_DROP);
        return false;
    }

    // 2) Bộ đệm chung đầy -> push-out theo thứ bậc ưu tiên:
    //    gói lớp c được đẩy bỏ gói ở đầu hàng đợi của các lớp thấp hơn (BE trước, rồi Critical)
    if (GetCurrentSize() + item > GetMaxSize())
    {
        if (m_pushOut && c != TC_BEST_EFFORT)
        {
            for (int v = TC_BEST_EFFORT; v > c; --v)
            {
                Ptr<InternalQueue> vq = GetInternalQueue(v);
                while (GetCurrentSize() + item > GetMaxSize() && !vq->IsEmpty())
                {
                    Ptr<QueueDiscItem> victim = vq->Dequeue();
                    DropAfterDequeue(victim, PUSHOUT_DROP);
                    m_pushOuts++;
                }
            }
        }
        if (GetCurrentSize() + item > GetMaxSize())
        {
            DropBeforeEnqueue(item, LIMIT_DROP);
            return false;
        }
    }

    bool ok = q->Enqueue(item);
    NS_LOG_LOGIC("class " << +c << " enqueued=" << ok);
    return ok;
}

Ptr<QueueDiscItem>
EmergencyQueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);
    RefillTokens();

    // Giai đoạn 1: ưu tiên tuyệt đối cho khẩn cấp trong phạm vi token
    Ptr<InternalQueue> eq = GetInternalQueue(TC_EMERGENCY);
    if (!eq->IsEmpty())
    {
        uint32_t sz = eq->Peek()->GetSize();
        if (m_tokens >= sz)
        {
            m_tokens -= sz;
            m_strictServed++;
            return eq->Dequeue();
        }
    }

    bool backlog = false;
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (!GetInternalQueue(i)->IsEmpty())
        {
            backlog = true;
            break;
        }
    }
    if (!backlog)
    {
        return nullptr;
    }

    // Giai đoạn 2: Deficit Round Robin (khẩn cấp vượt hạn mức bị hạ cấp vào đây)
    const std::array<uint32_t, 3> quantum{m_quantumE, m_quantumC, m_quantumBE};
    while (true)
    {
        Ptr<InternalQueue> q = GetInternalQueue(m_rr);
        if (q->IsEmpty())
        {
            m_deficit[m_rr] = 0;
            NextClass();
            continue;
        }
        if (!m_quantumGranted)
        {
            m_deficit[m_rr] += quantum[m_rr];
            m_quantumGranted = true;
        }
        int64_t sz = q->Peek()->GetSize();
        if (m_deficit[m_rr] >= sz)
        {
            m_deficit[m_rr] -= sz;
            Ptr<QueueDiscItem> item = q->Dequeue();
            if (m_rr == TC_EMERGENCY)
            {
                m_demotedServed++;
            }
            if (q->IsEmpty())
            {
                m_deficit[m_rr] = 0;
                NextClass();
            }
            return item;
        }
        NextClass();
    }
}

bool
EmergencyQueueDisc::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("EmergencyQueueDisc cannot have classes");
        return false;
    }
    if (GetNInternalQueues() == 0)
    {
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize", QueueSizeValue(m_emergencyLimit)));
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize", QueueSizeValue(GetMaxSize())));
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize", QueueSizeValue(GetMaxSize())));
    }
    if (GetNInternalQueues() != 3)
    {
        NS_LOG_ERROR("EmergencyQueueDisc needs exactly 3 internal queues");
        return false;
    }
    return true;
}

void
EmergencyQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);
    m_tokens = m_emergencyBurst;
    m_lastRefill = Simulator::Now();
    m_deficit = {0, 0, 0};
    m_rr = TC_EMERGENCY;
    m_quantumGranted = false;
}

} // namespace ns3
