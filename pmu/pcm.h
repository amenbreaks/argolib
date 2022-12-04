#include <chrono>
#include "cpucounters.h" //PCM related: https://github.com/intel/pcm
#include "utils.h"       //PCM related: https://github.com/intel/pcm

#define millisleep(a) boost::this_fiber::sleep_for(std::chrono::milliseconds(a))

namespace logger {
  static pcm::PCM *___pcm;
  pcm::SystemCounterState ___before_sstate, ___after_sstate;
  double start_time;
  static int init=0;

  void start() {
    /////////////////////////////////////////////////////////////////////////
    ////////// START INITIALIZATION ----- MUST BE DONE ONLY ONCE ////////////
    /////////////////////////////////////////////////////////////////////////
    if(init) return;
    ___pcm = pcm::PCM::getInstance();
    if (init==0) {
        ___pcm->resetPMU();
	init=1;
    }
    // program() creates common semaphore for the singleton, so ideally to be called before any other references to PCM
    pcm::PCM::ErrorCode status = ___pcm->program();

    switch (status)
    {
    case pcm::PCM::Success:
	//std::cerr << "Success initializing PCM" << std::endl;
        break;
    case pcm::PCM::MSRAccessDenied:
        std::cerr << "Access to Processor Counter Monitor has denied (no MSR or PCI CFG space access)." << std::endl;
        exit(EXIT_FAILURE);
    case pcm::PCM::PMUBusy:
        std::cerr << "Access to Processor Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU." << std::endl;
        std::cerr << "Alternatively you can try running PCM with option -r to reset PMU configuration at your own risk." << std::endl;
        exit(EXIT_FAILURE);
    default:
        std::cerr << "Access to Processor Counter Monitor has denied (Unknown error)." << std::endl;
        exit(EXIT_FAILURE);
    }
    /////////////////////////////////////////////////////////////////////////
    ////////// END OF INITIALIZATION ----- MUST BE DONE ONLY ONCE ///////////
    /////////////////////////////////////////////////////////////////////////

    // ___before_sstate = pcm::getSystemCounterState();
  }

  double end() {
    ___after_sstate = pcm::getSystemCounterState();
    double _joules = getConsumedJoules(___before_sstate, ___after_sstate);
    uint64_t instrtd = getInstructionsRetired(___before_sstate, ___after_sstate);
    return _joules / instrtd;
  }
} //namespace logger

