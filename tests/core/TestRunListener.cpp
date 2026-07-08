// Registers the shared run listener (see tests/OrkigeTestListener.h): clears
// the game-object world before static teardown to prevent the exit-time
// destruction-order segfault.
#include "../OrkigeTestListener.h"
CATCH_REGISTER_LISTENER(Orkige::OrkigeTestRunListener)
