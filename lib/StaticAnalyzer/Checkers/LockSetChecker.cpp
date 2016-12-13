#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

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

using namespace clang;
using namespace ento;

// ####################
//
// CHECKER DEFINITION
//
// ####################

namespace {
    class LockSetChecker : public Checker< check::PreCall,
                                           check::EndFunction,
                                           check::Location > {

    // but type
    mutable std::unique_ptr<BuiltinBug> BT_raceCondition;

    public:
        void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
        void checkEndFunction(CheckerContext &C) const;
        void checkLocation(SVal Loc, bool IsLoad, const Stmt *S, CheckerContext &C) const;
        // TODO memory location read/write hook WITH ERRORS

        // utility functions
        static unsigned int getState(unsigned int curr_state, int op, int thread);
        static void printState(unsigned state);
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
// append to the end of list, and can only remove the first element)
REGISTER_TRAIT_WITH_PROGRAMSTATE(ActiveThread, unsigned)
REGISTER_MAP_WITH_PROGRAMSTATE(PrevThreadMap, unsigned, unsigned)

// lock memory regions -> virtual Lid (lock id)
// monotonic lock ID. Can only handle 63 locks *globally*
REGISTER_TRAIT_WITH_PROGRAMSTATE(Lid, unsigned)
REGISTER_MAP_WITH_PROGRAMSTATE(LocLockMap, const MemRegion *, unsigned)

// tid -> locks held
// locks held by each thread. 8 byte bit field
REGISTER_MAP_WITH_PROGRAMSTATE(ThreadLockMap, unsigned, unsigned long)

// mem locations -> lockset
REGISTER_MAP_WITH_PROGRAMSTATE(LockSetMap, const MemRegion *, unsigned long)
// mem locations -> state
REGISTER_MAP_WITH_PROGRAMSTATE(StateMap, const MemRegion *, unsigned long)

// TODO
// add the memory location -> state map

// ####################
//
// CHECKER IMPLEMENTATION
//
// ####################

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

// extremely useful for dubugging
void LockSetChecker::printState(unsigned state) {
    int tid;
    switch ( (char) state ) {
        case STATE_VIRGIN:
            puts("VIRGIN");
            break;
        case STATE_EXCLUSIVE:
            tid = state >> 8;
            printf("EXCLUSIVE(%d)\n", tid);
            break;
        case STATE_SHARED:
            puts("SHARED");
            break;
        default:
            puts("SHARED_MODIFIED");
    }
}


// TODO maybe we could parameterize these strings somehow?
// make it "flexible"

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
    const LocationContext *LCtx = C.getLocationContext();

    // get the calling function identifier
    const IdentifierInfo *ID = Call.getCalleeIdentifier();

    // I don't think this will ever actually be NULL
    if (ID == NULL) {
        puts("call event ID is null");
        return;
    }

    // allias function name
    // std::string is seamlessly cast to llvm::StringRef
    llvm::StringRef funcName = ID->getName();

    // update the virtual thread information
    if (isSpawn(funcName)) {

        // get the relevant state
        TidTy tid = s->get<Tid>() + 1;
        ActiveThreadTy activeThread = s->get<ActiveThread>();

        //printf("%s: %d -> %d\n", funcName.str().c_str(), activeThread, tid);

        // set the new state
        s = s->set<PrevThreadMap>(tid, activeThread);
        s = s->set<ActiveThread>(tid);
        // incement the thread counter
        s = s->set<Tid>(tid);

    } else if (isLock(funcName) || isUnlock(funcName)) {

        // get memory region of mutex argument
        SVal arg = s->getSVal(Call.getArgExpr(0), LCtx);
        // return immediately if the lock is unknown
        if (arg.isUnknownOrUndef())
            return;

        // get lock as memory region
        const MemRegion *lockLoc = arg.getAsRegion()->StripCasts();

        // this is here just in case
        if (!lockLoc) {
            puts("lock arg is not mem region");
            return;
        }

        // get the current lock id
        LidTy lid = s->get<Lid>();

        if (lid == 64) {
            puts("maximum number of locks exceeded (63)");
            return;
        }

        // get the virtual lock number.
        // initialize the locLock map if first time processing lock
        const unsigned *lockNumPtr = s->get<LocLockMap>(lockLoc);
        unsigned lockNum;
        if (lockNumPtr == NULL) {
            s = s->set<LocLockMap>(lockLoc, lid);
            lockNum = lid;
            s = s->set<Lid>(++lid);
        } else
            lockNum = *lockNumPtr;

        // get active thread
        ActiveThreadTy activeThread = s->get<ActiveThread>();

        // get the locks held bit field
        // initialize the ThreadLoc map if first time processing thread
        const unsigned long *locksHeldPtr = s->get<ThreadLockMap>(activeThread);
        unsigned long locksHeld;
        if (locksHeldPtr == NULL) {
            s = s->set<ThreadLockMap>(activeThread, LOCK_SET_EMPTY);
            locksHeld = LOCK_SET_EMPTY;
        } else
            locksHeld = *locksHeldPtr;

        // set/unset the correct bit in locksHeld
        if (isLock(funcName))
            locksHeld |= (1 << lockNum);
        else
            locksHeld &= ~(1 << lockNum);

        // set the new state
        s = s->set<ThreadLockMap>(activeThread, locksHeld);

        // debugging
        /*
        if (isLock(funcName))
            printf("%2dL: ", activeThread);
        else
            printf("%2dU: ", activeThread);
        for (int i = 0; i < 4; i++)
            printf("%lu", (locksHeld >> i) & 1);
        printf("\n");
        */

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
    std::string funcName = callSiteFunc->getNameInfo().getAsString();

    // this is a huge hack, but the checker executes on
    // on functions that aren't called from main after
    // prqcessing main. This doesn't make sense for our
    // analysis.

    // FIXME FIXME FIXME
    // if (funcName == "main")
    //     exit(0);

    if ( isSpawn(funcName) ) {

        // get the relevant state
        ActiveThreadTy activeThread = s->get<ActiveThread>();
        unsigned prevThread = *(s->get<PrevThreadMap>(activeThread));

        //printf("ret: %d -> %d\n", activeThread, prevThread);

        // update the state
        s = s->set<ActiveThread>(prevThread);
        s = s->remove<PrevThreadMap>(activeThread);
    }

    // push state into context
    C.addTransition(s);
}

void LockSetChecker::checkLocation(SVal Loc, bool IsLoad, const Stmt *S, CheckerContext &C) const {

    // give up immediately in loc is undefined
    if (Loc.isUnknownOrUndef())
        return;

    ProgramStateRef s = C.getState();

    // note that we strip casts from memory region
    const MemRegion *loc = Loc.getAsRegion()->StripCasts();

    /*
    if (loc2 != NULL)
        printf("raw: %s\nret: %s\n", reg, loc2->getSyk.c_str());
    */

    // this is here just in case
    if (!loc) {
        puts("lock arg is not mem region");
        return;
    }

    // get active thread
    ActiveThreadTy activeThread = s->get<ActiveThread>();

    // get the locks held bit field
    // initialize the ThreadLoc map if first time processing thread
    const unsigned long *locksHeldPtr = s->get<ThreadLockMap>(activeThread);
    unsigned long locksHeld;
    if (locksHeldPtr == NULL) {
        s = s->set<ThreadLockMap>(activeThread, LOCK_SET_EMPTY);
        locksHeld = LOCK_SET_EMPTY;
    } else
        locksHeld = *locksHeldPtr;

    // get the lockset bit field
    const unsigned long *lockSetPtr = s->get<LockSetMap>(loc);
    unsigned long lockSet;
    if (lockSetPtr == NULL)
        lockSet = LOCK_SET_UNIVERSE;
    else
        lockSet = *lockSetPtr;

    // get the current state
    const unsigned long *locStatePtr = s->get<StateMap>(loc);
    unsigned long locState;
    if (locStatePtr == NULL)
        locState = STATE_VIRGIN;
    else
        locState = *locStatePtr;

    // do the intersection
    // secret magic sauce
    if (lockSet == LOCK_SET_UNIVERSE)
        lockSet = locksHeld;
    else
        lockSet &= locksHeld;

    // update the location state

    //puts("---------");
    //printState(locState);

    /*
    if (activeThread == 1)
    {
      if (IsLoad)
          printf("%d: R: %s\n", activeThread, loc->getString().c_str());
      else
          printf("%d: W: %s\n", activeThread, loc->StripCasts()->getString().c_str());
    }
    */

    int op = IsLoad ? OP_READ : OP_WRITE;
    locState = getState(locState, op, activeThread);

    //printState(locState);
    //puts("---------");

    if (lockSet == LOCK_SET_EMPTY && locState == STATE_SHARED_MODIFIED) {
        // generate error node -- keep analyzing
        // This should be improved to provide more detail
        ExplodedNode *N = C.generateNonFatalErrorNode(s);
        if (N) {
            if (!BT_raceCondition)
                BT_raceCondition.reset(new BuiltinBug(this, "LockSet checker",
                                                            "Race condition detected on this memory access. "
                                                            "Check locking discpline." ));
            C.emitReport(llvm::make_unique<BugReport>(*BT_raceCondition,
                BT_raceCondition->getDescription(), N));
        }
    }

    s = s->set<StateMap>(loc, locState);
    s = s->set<LockSetMap>(loc, lockSet);

    // push state into context
    C.addTransition(s);
}

// Registration code (not sure how this works exactly)
void ento::registerLockSetChecker(CheckerManager &mgr) {
  mgr.registerChecker<LockSetChecker>();
}
