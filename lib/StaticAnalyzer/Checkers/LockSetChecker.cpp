#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

// ####################
//
// "TYPE" Definitions
// (more flexible than enum)
//
// ####################

// defines read/write operations
#define OP_READ         0
#define OP_WRITE        1

// location state is encoded as 4 byte bit field. The bottom 8 bits are reserved
// for the state type below. The top bits are reserved for the associated tid
// required for the exclusive state.
#define STATE_VIRGIN           0
#define STATE_EXCLUSIVE        1
#define STATE_SHARED           2
#define STATE_SHARED_MODIFIED  3

// Useful lock set values. Sets are encoded as an 8 byte bit field.
// An empty set has the value 0. A special set representing "all posible locks"
// has the topmost bit set. In this scheme, set intersection is just bitwise AND.
// The downside is that we can only handle 63 locks. This is probably enough.
#define LOCK_SET_EMPTY    0
#define LOCK_SET_UNIVERSE 0x8000000000000000

// ####################
//
// CHECKER DEFINITION
//
// ####################

namespace {
    class LockSetChecker : public Checker< check::PreCall,
                                           check::EndFunction > {
    public:
        void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
//    void checkPostStmt(const Stmt *S, CheckerContext &C) const;
        void checkEndFunction(CheckerContext &C) const;

        // utility functions
        static unsigned int getState(unsigned int curr_state, int op, int thread);
        static inline bool isLock(llvm::StringRef s);
        static inline bool isUnlock(llvm::StringRef s);
        static inline bool isSpawn(llvm::StringRef s);
    };
};

// ####################
//
// STATE REGISTRATION
//
// ####################

// NOTE for some reason "unsigned" was the only integer that doesn't
// throw type errors. I ave no idea why.

// monotonic thread identifier
REGISTER_TRAIT_WITH_PROGRAMSTATE(Tid, unsigned)

// Store the current active thread AND a map from tid to previous
// tid. This implements a stack. Encoding a stack with the built
// in REGISTER_LIST_WITH_PROGRAMSTATE was not possible (can only
// append to the end of list, and can only remove the first element.
REGISTER_TRAIT_WITH_PROGRAMSTATE(ActiveThread, unsigned)
REGISTER_MAP_WITH_PROGRAMSTATE(PrevThreadMap, unsigned, unsigned)

// given a current location state, operation, and tid, returns the new state.
// This is just the state machine diagram from the lockset/eraser paper.
unsigned LockSetChecker::getState(unsigned curr_state, int op, int tid) {

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

inline bool LockSetChecker::isLock(llvm::StringRef s) {
    return s == "pthread_mutex_lock";
}

inline bool LockSetChecker::isUnlock(llvm::StringRef s) {
    return s == "pthread_mutex_unlock";
}

inline bool LockSetChecker::isSpawn(llvm::StringRef s) {
    return s.substr(0, 2) == "__";
}

// prints the names of symbolically executed functions
void LockSetChecker::checkPreCall(const CallEvent &Call, CheckerContext &C) const {

    ProgramStateRef s = C.getState();

    // get the calling function identifier
    const IdentifierInfo *ID = Call.getCalleeIdentifier();

    // I don't think this will ever actually be NULL
    if (ID == NULL)
        return;

    if ( isSpawn(ID->getName()) ) {

        // get the relevant state
        TidTy tid = s->get<Tid>() + 1;
        ActiveThreadTy activeThread = s->get<ActiveThread>();

        printf("%d -- %s -> %d\n", activeThread, ID->getName().str().c_str(), tid);

        // set the new state
        s = s->set<PrevThreadMap>(tid, activeThread);
        s = s->set<ActiveThread>(tid);
        // incement the thread counter
        s = s->set<Tid>(tid);
    }

    // push state into context
    C.addTransition(s);
}

// pops a thread from the thread "stack" (actually a map)
void LockSetChecker::checkEndFunction(CheckerContext &C) const {

    ProgramStateRef s = C.getState();

    // get the calling function declaration
    FunctionDecl* callSiteFunc = (FunctionDecl*)C.getStackFrame()->getDecl();
    // get the function name from the declaration
    std::string fname = callSiteFunc->getNameInfo().getAsString();

    if ( isSpawn(fname) ) {

        // get the relevant state
        ActiveThreadTy activeThread = s->get<ActiveThread>();
        unsigned prevThread = *(s->get<PrevThreadMap>(activeThread));

        printf("%d <- ret -- %d\n", prevThread, activeThread);

        // update the state
        s = s->set<ActiveThread>(prevThread);
        s = s->remove<PrevThreadMap>(activeThread);
    }

    // push state into context
    C.addTransition(s);

}

// Registration code (not sure how this works exactly)
void ento::registerLockSetChecker(CheckerManager &mgr) {
  mgr.registerChecker<LockSetChecker>();
}
