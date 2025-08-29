#include <iostream>

#ifdef WHISPER_AVAILABLE
#include "whisper.h"
#endif

int main() {
    std::cout << "Testing Whisper.cpp integration..." << std::endl;
    
#ifdef WHISPER_AVAILABLE
    std::cout << "Whisper.cpp is available!" << std::endl;
    std::cout << "Whisper version: " << whisper_version() << std::endl;
    
    // Test basic functionality
    whisper_context_params ctx_params = whisper_context_default_params();
    std::cout << "Default context params created successfully" << std::endl;
    
    return 0;
#else
    std::cout << "Whisper.cpp is NOT available - compilation will use simulation mode" << std::endl;
    return 1;
#endif
}