#include <stdio.h>

#include "interface_director.h"


interface_director::interface_director() {}

interface_director::~interface_director() {}

void interface_director::display_data(std::string data) {

	printf("%s\n", data.c_str());
}


void interface_director::display_status(std::string status) {

	printf("%s\n", status.c_str());
}
