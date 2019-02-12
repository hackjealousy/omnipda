#ifndef INCLUDED_OMNIPOD_PDA_H
#define INCLUDED_OMNIPOD_PDA_H

#include <gr_block.h>
#include <gr_complex.h>
#include <pthread.h>
#include <limits.h>

#include "interface_director.h"


typedef enum {
	ST_IDLE,
	ST_ON,
	ST_STATUS,
	ST_STATUS_ON_SENT
} e_state;


class omnipod_pda;

typedef boost::shared_ptr<omnipod_pda> omnipod_pda_sptr;
omnipod_pda_sptr omnipod_make_pda(double sr, interface_director *id);

class omnipod_pda : public gr_block {
public:
	~omnipod_pda();
	int general_work(int noutput_items, gr_vector_int &ninput_items, gr_vector_const_void_star &input_items, gr_vector_void_star &output_items);
	void forecast(int noutput_items, gr_vector_int &ninput_items_required);

	void set_monitor(int on);
	void start_status();
	void set_secret(unsigned int);
	void set_seqno(unsigned int);

	void display_data(const char *, ...);
	void display_status(const char *, ...);

private:
	friend omnipod_pda_sptr omnipod_make_pda(double sr, interface_director *id);
	omnipod_pda(double sr, interface_director *id);

	interface_director *m_id;

	e_state		m_state;
	pthread_mutex_t m_state_mutex;			// only state is accessed by multiple threads

	double		m_sr;				// sample rate
	unsigned int	m_sps;				// samples per symbol (symbol is half a bit)

	// rx variables
	unsigned int	m_jitter;			// must hold for at least this many samples to count

	unsigned int	m_average_len;			// number of samples in average (m_avg_n * m_sps)
	double		m_average_a;			// average of samples after current sample
	double		m_average_b;			// average of samples before current sample

	int		m_sign;				// last sample was over / under average
	unsigned int	m_count;			// count of over / under
	unsigned int	m_change_count;			// don't change sign unless passed jitter threshold

	unsigned char	m_rx_buf[BUFSIZ];		// buffer for incoming demodulated signal
	unsigned int	m_rx_buf_count;			// number of symbols (bytes) in rx_buf
	unsigned long long m_rx_buf_received;		// sample rx_buf starts at
	unsigned long long m_rx_last_buf_received;	// sample last buf started at

	int		m_rx_enabled;			// enabled if processing rx

	char *		m_rx_decoded;			// decoded rx packet for processing
	unsigned int	m_rx_decoded_len;		// length of decoded rx packet
	unsigned long long m_rx_decoded_received;	// sample decoded rx packet starts at

	int		m_monitor;			// monitor mode

	unsigned long long m_rx_sample_number;		// current rx sample number

	// tx variables
	gr_complex *	m_zero;				// encoded and modulated zero
	gr_complex *	m_one;				// encoded and modulated one
	gr_complex *	m_hv;				// encoded and modulated high violation
	gr_complex *	m_lv;
	unsigned int	m_bitlen;			// length of encoded and modulated zero / one
	unsigned int	m_hv_len;			// length of encoded and modulated high violation
	unsigned int	m_lv_len;

	gr_complex *	m_tx_buf;			// buffer for encoded and modulated signal
	unsigned int	m_tx_buf_count;			// number of samples in tx_buf
	unsigned int	m_tx_buf_cur;			// current index in tx_buf

	int		m_tx_enabled;			// enabled if transmitting
	unsigned long long m_tx_at;			// (re)transmit m_tx_buf starting at this rx sample number
	unsigned int	m_retransmit_num;

	unsigned long long m_tx_sample_number;		// current tx sample number

	long long int	m_secret;			// "secret" number for communication
	int		m_seqno;			// current sequence number


	// constants
	static const double	  m_symbol_rate = 4000;	// deduced symbol rate (bit rate is half this)
	static const unsigned int m_avg_n = 8;		// average over this many symbols
	// static const double	  m_error = 0.15;	// max error in symbol width
	static const double	  m_error = 0.30;	// max error in symbol width (XXX 0.25 is very wide)
	static const unsigned int m_retransmit_max = 10;

	static const unsigned long long m_at_never = ULLONG_MAX;

	// private functions
	void decode_rx_symbols();
	void slice();
	void process_rx_sample(float cur);
	void process_decoded();
	unsigned int process_tx(gr_complex *output, int noutput);
	e_state get_state();
	int get_monitor();
	void build_packet(char *data, unsigned int data_len);
	void display_c_hex_bytes(char *data, unsigned int data_len, unsigned long long, unsigned long long);
	void transmit_packet(char *data, unsigned int data_len);
	void transmit_on_packet();
};
#endif /* !INCLUDED_OMNIPOD_PDA_H */
