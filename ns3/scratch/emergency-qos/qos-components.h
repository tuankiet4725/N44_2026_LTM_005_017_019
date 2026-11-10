/*
 * Emergency-aware QoS components for ns-3 (tested target: ns-3.40+)
 *
 *  - DscpPacketFilter   : phân loại gói IPv4 theo DSCP -> 3 lớp lưu lượng
 *  - EmergencyQueueDisc : (EPQ) thuật toán đề xuất
 *        * Ưu tiên tuyệt đối cho lớp KHẨN CẤP, giới hạn bởi token bucket
 *          (chống "đói" băng thông các lớp khác khi bị tấn công/flood)
 *        * Khi hết token, gói khẩn cấp KHÔNG bị hủy mà bị "hạ cấp" sang
 *          lập lịch DRR cùng các lớp khác
 *        * Push-out theo thứ bậc: khi bộ đệm chung đầy, gói lớp cao đẩy bỏ
 *          gói lớp thấp hơn (Best-Effort trước, rồi Critical) thay vì bị hủy
 *        * DRR có trọng số giữa CRITICAL và BEST_EFFORT
 */
#ifndef EMERGENCY_QOS_COMPONENTS_H
#define EMERGENCY_QOS_COMPONENTS_H

#include "ns3/data-rate.h"
#include "ns3/nstime.h"
#include "ns3/packet-filter.h"
#include "ns3/queue-disc.h"
#include "ns3/queue-size.h"

#include <array>

namespace ns3
{

enum TrafficClass : uint8_t
{
    TC_EMERGENCY = 0,  // DSCP EF(46), CS6(48)
    TC_CRITICAL = 1,   // DSCP CS4/AF4x/CS5 (video giám sát, điều khiển)
    TC_BEST_EFFORT = 2 // còn lại
};

/** Ánh xạ DSCP -> lớp lưu lượng (dùng chung cho filter và EPQ). */
uint8_t DscpToTrafficClass(uint8_t dscp);

/** Packet filter phân loại theo DSCP, dùng cho PrioQueueDisc (baseline). */
class DscpPacketFilter : public PacketFilter
{
  public:
    static TypeId GetTypeId();
    DscpPacketFilter();
    ~DscpPacketFilter() override;

  private:
    bool CheckProtocol(Ptr<QueueDiscItem> item) const override;
    int32_t DoClassify(Ptr<QueueDiscItem> item) const override;
};

/** Emergency-aware Priority Queue (EPQ) - thuật toán đề xuất. */
class EmergencyQueueDisc : public QueueDisc
{
  public:
    static TypeId GetTypeId();
    EmergencyQueueDisc();
    ~EmergencyQueueDisc() override;

    static constexpr const char* LIMIT_DROP = "Queue limit exceeded";
    static constexpr const char* PUSHOUT_DROP = "Pushed out by emergency packet";

    uint32_t GetPushOutCount() const;
    uint32_t GetStrictServedCount() const;
    uint32_t GetDemotedServedCount() const;

  private:
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    bool CheckConfig() override;
    void InitializeParams() override;

    uint8_t ClassOf(Ptr<const QueueDiscItem> item) const;
    void RefillTokens();
    void NextClass();

    // --- tham số (attributes) ---
    DataRate m_emergencyRate;  //!< tốc độ token cho phục vụ ưu tiên tuyệt đối
    uint32_t m_emergencyBurst; //!< kích thước xô token (byte)
    QueueSize m_emergencyLimit; //!< giới hạn hàng đợi khẩn cấp
    bool m_pushOut;            //!< bật/tắt cơ chế push-out
    uint32_t m_quantumE;       //!< quantum DRR khi gói khẩn cấp bị hạ cấp
    uint32_t m_quantumC;       //!< quantum DRR lớp critical
    uint32_t m_quantumBE;      //!< quantum DRR lớp best-effort

    // --- trạng thái ---
    double m_tokens;
    Time m_lastRefill;
    std::array<int64_t, 3> m_deficit;
    uint32_t m_rr;
    bool m_quantumGranted;

    // --- thống kê ---
    uint32_t m_pushOuts;
    uint32_t m_strictServed;
    uint32_t m_demotedServed;
};

} // namespace ns3

#endif
