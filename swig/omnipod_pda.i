%module(directors="1") omnipod

%feature("director") interface_director;

%include "gnuradio.i"
%{
#include "interface_director.h"
#include "omnipod_pda.h"
%}

%include "../src/interface_director.h"

GR_SWIG_BLOCK_MAGIC(omnipod, pda);
omnipod_pda_sptr omnipod_make_pda(double, interface_director *id);

class omnipod_pda : public gr_block {

public:
        void set_monitor(int);
        void start_status();
        void set_secret(unsigned int);
        void set_seqno(unsigned int);

        void display_data(const char *);
        void display_status(const char *);

private:
        omnipod_pda(double, interface_director *id);
};
