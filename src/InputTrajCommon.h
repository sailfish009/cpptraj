#ifndef INC_INPUTTRAJCOMMON_H
#define INC_INPUTTRAJCOMMON_H
#include "Topology.h"
#include "TrajFrameCounter.h"
/// Input trajectory/ensemble common functionality.
class InputTrajCommon {
  public:
    InputTrajCommon() : trajParm_(0) {}
    FileName const& Filename()        const { return trajName_; }
    // TODO: return const&, modify all TrajectoryIO routines?
    Topology* Parm()                  const { return trajParm_; }
    TrajFrameCounter const& Counter() const { return frameCount_; }
    TrajFrameCounter& Counter()             { return frameCount_; }
    /// Set trajectory file name and associated Topology.
    int SetNameAndParm(std::string const&, Topology*);
  private:
    TrajFrameCounter frameCount_; ///< Frame counter for GetNextX routines.
    FileName trajName_;           ///< Trajectory file name (lowest rep for ensembles).
    Topology* trajParm_; ///< Pointer to Topology associated with trajectory/ensemble.
};
#endif
