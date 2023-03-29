#pragma once

#include "threadinfo.h"

namespace NVMMgr_ns {
class EpochGuard {
  public:
    // EpochGuard() { JoinNewEpoch(); } // 将last_active_epoch设为现在的epoch
    // ~EpochGuard() { LeaveThisEpoch(); } // 将last_active_epoch设为-1
    static void DeleteNode(void *node) { MarkNodeGarbage(node); }
};
} // namespace NVMMgr_ns