#include "crt.h"

#include "Person.h";

void Person::setName(char* name) {
    this->_name = name;
}

void Person::sayHello(char* message) {
    printf("Hello World! My name is %s. This is my message: %s", this->_name, message);
}