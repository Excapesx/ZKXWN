#pragma once
#include <string>

namespace rmtc102 {
struct ReceiptObservation {
    int expectedTick = -1, receiptTick = -1, gap = 0;
    bool missingReceipt = false;
};
// Diagnostic only. A missed receipt is never evidence of a particular network
// fault; never resend old tick actions or alter decisions based on this audit.
class ReceiptAudit {
public:
    void reset() { session_.clear(); lastSent_ = -1; lastKind_.clear(); }
    ReceiptObservation observe(const std::string& session, int tick, int receipt, const std::string& receiptKind={}) {
        if (session != session_) { reset(); session_ = session; }
        ReceiptObservation r; r.receiptTick = receipt;
        if (lastSent_ >= 0 && tick > lastSent_) {
            r.expectedTick = lastSent_; r.gap = tick - lastSent_ - 1;
            // Only the immediate next frame can confirm this exact action.
            // Skipped perceptions cannot be interpreted as lost actions.
            r.missingReceipt = r.gap == 0 && (receipt != lastSent_ ||
                (!receiptKind.empty() && !lastKind_.empty() && receiptKind!=lastKind_));
        }
        return r;
    }
    void sent(int tick, const std::string& kind={}) { lastSent_ = tick; lastKind_=kind; }
private:
    std::string session_,lastKind_;
    int lastSent_ = -1;
};
}  // namespace rmtc102
