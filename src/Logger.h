//
// Created by Valerian on 2024/2/26.
//

#ifndef EVA_LOGGER_H
#define EVA_LOGGER_H
#include <iostream>
#include <sstream>
class ErrorLogMessage : public std::basic_ostringstream<char>{
public:
    ~ErrorLogMessage(){
        std::cerr << "Fatal error: " << str().c_str();
        exit(EXIT_FAILURE);
    }
};
#define DIE ErrorLogMessage()
#endif //EVA_LOGGER_H