#pragma once

#include "crt.h"

class Person {
private:
    char* _name;
public:
    void setName(char* name);
    void sayHello(char* message);
};
