#ifndef INTERFACE_DIRECTOR_H
#define INTERFACE_DIRECTOR_H

#include <string>

class interface_director {

public:
	interface_director();
	virtual ~interface_director();

	virtual void display_data(std::string d);
	virtual void display_status(std::string s);
};

#endif /* !INTERFACE_DIRECTOR_H */
