#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

#define OP_READ         0
#define OP_WRITE        1
// location state is encoded as 4 byte bit field. The bottom 8 bits are reserved 
// for the state type below. The top bits are reserved for the associated tid
// required for the exclusive state
#define STATE_VIRGIN           0
#define STATE_EXCLUSIVE        1 
#define STATE_SHARED           2
#define STATE_SHARED_MODIFIED  3

namespace {
    class LockSetChecker : public Checker<check::PreCall> {
    public:
      void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
      static unsigned int getState(int curr_state, int op, int thread);
    };
};

// given a current location state, operation, and tid, returns the new state. 
// This is just the state machine diagram from the lockset/eraser paper.
static unsigned int LockSetChecker::getState(unsigned int curr_state, int op, int tid) {
    
    // switch on the bottom byte of the current state
    switch ( (char) curr_state ) {

        case STATE_VIRGIN:

            switch (op) {
                case OP_READ:
                    return curr_state;
                case OP_WRITE:
                    return STATE_EXCLUSIVE | tid << 8 ; 
            }

        case STATE_EXCLUSIVE:

            if ( curr_state >> 8  == tid)
                return curr_state;

            switch (op) {
                case OP_READ:
                    return STATE_SHARED;
                case OP_WRITE:
                    return STATE_SHARED_MODIFIED;
            }

        case STATE_SHARED:

            switch (op) {
                case OP_READ:
                    return curr_state;
                case OP_WRITE:
                    return STATE_SHARED_MODIFIED;
            }

    }

    // STATE_SHARED_MODIFIED is the "sink"
    return curr_state;
}

// prints the names of symbolically executed functions
void LockSetChecker::checkPreCall(const CallEvent &Call, CheckerContext &C) const {

    const IdentifierInfo *ID = Call.getCalleeIdentifier();

    if (ID == NULL)
        return;

    printf("%s\n", ID->getName());

}

// Registration code (not sure how this works exactly)
void ento::registerLockSetChecker(CheckerManager &mgr) {
  mgr.registerChecker<LockSetChecker>();
}
