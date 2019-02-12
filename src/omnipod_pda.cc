#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdexcept>
#include <limits.h>
#include <pthread.h>

#include "omnipod_pda.h"
#include "utils.h"

#include <gr_io_signature.h>
#include <gr_complex.h>


omnipod_pda_sptr omnipod_make_pda(double sr, interface_director *id) {

	return omnipod_pda_sptr(new omnipod_pda(sr, id));
}


void omnipod_pda::forecast(int, gr_vector_int &ninput_items_required) {

	ninput_items_required[0] = 2 * m_average_len + 1 + 1;
}


static const int MIN_IN = 1;
static const int MAX_IN = 1;
static const int MIN_OUT = 1;
static const int MAX_OUT = 1;


omnipod_pda::omnipod_pda(double sr, interface_director *id) :
   gr_block("omnipod_pda",
   gr_make_io_signature(MIN_IN, MAX_IN, sizeof(gr_complex)),
   gr_make_io_signature(MIN_OUT, MAX_OUT, sizeof(gr_complex)))
{
	m_id = id;

	m_state = ST_IDLE;
	if(pthread_mutex_init(&m_state_mutex, 0))
		throw std::runtime_error("error: pthread_mutex_init");

	m_sr = sr;
	m_sps = (unsigned int)round(m_sr / m_symbol_rate);

	// rx variables
	m_jitter = m_sps / 4;

	m_average_len = m_avg_n * m_sps;
	m_average_a = 0;
	m_average_b = 0;

	m_sign = -1;
	m_count = 0;
	m_change_count = 0;

	m_rx_buf_count = 0;
	m_rx_buf_received = 0;
	m_rx_last_buf_received = 0;

	m_rx_decoded = 0;
	m_rx_decoded_len = 0;
	m_rx_decoded_received = 0;

	m_rx_sample_number = 0;

	m_monitor = 1;

	// tx variables
	m_bitlen = 2 * m_sps;
	m_lv_len = m_hv_len = m_sps / 2;

	if(!(m_zero = new gr_complex[m_bitlen]))
		throw std::runtime_error("error: cannot create zero buffer");
	if(!(m_one = new gr_complex[m_bitlen]))
		throw std::runtime_error("error: cannot create one buffer");
	if(!(m_hv = new gr_complex[m_hv_len]))
		throw std::runtime_error("error: cannot create hv buffer");
	if(!(m_lv = new gr_complex[m_hv_len]))
		throw std::runtime_error("error: cannot create lv buffer");

	unsigned int i;
	for(i = 0; i < m_sps; i++) {
		m_zero[i] = gr_complex(0, 0);
		m_one[i] = gr_complex(SHRT_MAX, 0);
	}
	for(; i < 2 * m_sps; i++) {
		m_zero[i] = gr_complex(SHRT_MAX, 0);
		m_one[i] = gr_complex(0, 0);
	}
	for(i = 0; i < m_hv_len; i++) {
		m_hv[i] = gr_complex(SHRT_MAX, 0);
		m_lv[i] = gr_complex(0, 0);
	}

	m_tx_buf = 0;
	m_tx_buf_count = 0;
	m_tx_buf_cur = 0;

	m_tx_enabled = 0;
	m_tx_at = m_at_never;
	m_retransmit_num = 0;

	m_tx_sample_number = 0;

	m_secret = -1;
	m_seqno = -1;

	set_history(2 * m_average_len + 1);
}


omnipod_pda::~omnipod_pda() {

	if(m_zero)
		delete[] m_zero;
	if(m_one)
		delete[] m_one;
	if(m_hv)
		delete[] m_hv;
	if(m_tx_buf)
		delete[] m_tx_buf;
	if(m_rx_decoded)
		delete[] m_rx_decoded;
}


void omnipod_pda::display_data(const char *fmt, ...) {

	char buf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);

	PyGILState_STATE gstate;

	gstate = PyGILState_Ensure();
	m_id->display_data(buf);
	PyGILState_Release(gstate);
}


void omnipod_pda::display_status(const char *fmt, ...) {

	char buf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);

	PyGILState_STATE gstate;

	gstate = PyGILState_Ensure();
	m_id->display_status(buf);
	PyGILState_Release(gstate);
}


void omnipod_pda::set_monitor(int on) {

	pthread_mutex_lock(&m_state_mutex);
	m_monitor = on;
	pthread_mutex_unlock(&m_state_mutex);

	if(m_monitor)
		display_status("Monitor mode is on");
	else
		display_status("Monitor mode is off");
}


void omnipod_pda::start_status() {

	pthread_mutex_lock(&m_state_mutex);
	if((m_state == ST_IDLE) && (m_secret >= 0) && (m_seqno >= 0)) {
		m_state = ST_STATUS;
		pthread_mutex_unlock(&m_state_mutex);
		display_status("Status protocol starting");
		return;
	}
	pthread_mutex_unlock(&m_state_mutex);
	display_status("Transaction already in progress");
}


void omnipod_pda::set_secret(unsigned int secret) {

	pthread_mutex_lock(&m_state_mutex);
	if(m_state != ST_IDLE) {
		pthread_mutex_unlock(&m_state_mutex);
		return;
	}
	m_secret = secret;
	pthread_mutex_unlock(&m_state_mutex);
}


void omnipod_pda::set_seqno(unsigned int seqno) {

	pthread_mutex_lock(&m_state_mutex);
	if(m_state != ST_IDLE) {
		pthread_mutex_unlock(&m_state_mutex);
		return;
	}
	m_seqno = seqno;
	pthread_mutex_unlock(&m_state_mutex);
}


e_state omnipod_pda::get_state() {

	e_state s;

	pthread_mutex_lock(&m_state_mutex);
	s = m_state;
	pthread_mutex_unlock(&m_state_mutex);

	return s;
}


int omnipod_pda::get_monitor() {

	int m;

	pthread_mutex_lock(&m_state_mutex);
	m = m_monitor;
	pthread_mutex_unlock(&m_state_mutex);

	return m;
}


void omnipod_pda::display_c_hex_bytes(char *data, unsigned int data_len, unsigned long long r, unsigned long long lr) {

	char *buf;
	unsigned int i, h = 0, h_count = 0, b_count = 0, bi = 0, bufsize;

	bufsize = 3 * data_len;
	if(bufsize < 1024)
		bufsize = 1024;

	if(!(buf = new char[bufsize])) {
		fprintf(stderr, "error: new\n");
		return;
	}

	// receieved time
	bi += snprintf(buf + bi, bufsize - bi, "%6.1lfms:\t", 1000.0 * (double)(r - lr) / m_sr);

	// hex representation
	for(i = 0; (i < data_len) && (bi < bufsize); i++) {
		if((data[i] == '0') || (data[i] == '1')) {
			h = (h << 1) | (data[i] - '0');
			h_count += 1;
			if(h_count >= 8) {
				if((b_count > 0) && (b_count % 4 == 0)) {
					bi += snprintf(buf + bi, bufsize - bi, " ");
					if(bi > bufsize)
						break;
				}
				bi += snprintf(buf + bi, bufsize - bi, "%2.2x", h);
				if(bi > bufsize)
					break;
				b_count += 1;
				h = 0;
				h_count = 0;
			}
		} else {
			if(h_count > 0) {
				h = h << (8 - h_count);
				if((b_count > 0) && (b_count % 4 == 0)) {
					bi += snprintf(buf + bi, bufsize - bi, " ");
					if(bi > bufsize)
						break;
				}
				bi += snprintf(buf + bi, bufsize - bi, "%2.2x", h);
				if(bi > bufsize)
					break;
				b_count += 1;
				h = 0;
				h_count = 0;
			}
			if(b_count > 0) {
				bi += snprintf(buf + bi, bufsize - bi, " ");
				if(bi > bufsize)
					break;
			}
			bi += snprintf(buf + bi, bufsize - bi, "%c", data[i]);
			if(bi > bufsize)
				break;
			b_count = 4;
		}
	}
	if((h_count > 0) && (bi < bufsize)) {
		h = h << (8 - h_count);
		if((b_count > 0) && (b_count % 4 == 0))
			bi += snprintf(buf + bi, bufsize - bi, " ");
		if(bi < bufsize)
			bi += snprintf(buf + bi, bufsize - bi, "%2.2x", h);
	}
	if(bi < bufsize)
		bi += snprintf(buf + bi, bufsize - bi, " : ");

	// bit representation
	int dno = 0;
	for(i = 0; (i < data_len) && (bi < bufsize - 4); i++) {
		if((data[i] == '0') || (data[i] == '1')) {
			if((dno > 0) && (dno % 4 == 0))
				buf[bi++] = ' ';
			buf[bi++] = data[i];
			dno += 1;
		} else {
			bi += snprintf(buf + bi, bufsize - bi, " %c ", data[i]);
			dno = 0;
		}
	}
	if(bi < bufsize)
		buf[bi] = 0;
	display_data(buf);
	delete[] buf;
}


void omnipod_pda::decode_rx_symbols() {

	char *rx_decoded;
	unsigned int rx_decoded_len, rx_decoded_received;

	if(!m_rx_buf_count)
		return;

	if(!(rx_decoded = new char[m_rx_buf_count * 4 + 1])) {
		fprintf(stderr, "error: cannot create decoded buf\n");
		return; // save rx_buf for later
	}
	rx_decoded_len = manchester_decode(m_rx_buf, m_rx_buf_count, rx_decoded, m_rx_buf_count * 4 + 1);
	rx_decoded_received = m_rx_buf_received;

	// erase received buffer for next burst
	m_rx_buf_count = 0;

	if(!rx_decoded_len) {
		delete[] rx_decoded;
		return;
	}

	if(m_monitor) {
		display_c_hex_bytes(rx_decoded, rx_decoded_len, m_rx_buf_received, m_rx_last_buf_received);
	}

	// XXX decide if we want to keep this

	/*
	if(m_rx_decoded) {
		delete[] m_rx_decoded;
		m_rx_decoded = 0;
		m_rx_decoded_len = 0;
		m_rx_decoded_received = 0;
	}
	m_rx_decoded = rx_decoded;
	m_rx_decoded_len = rx_decoded_len;
	m_rx_decoded_received = rx_decoded_received;
	 */

	// XXX do nothing now
	if(rx_decoded) {
		delete[] rx_decoded;
	}
}


void omnipod_pda::slice() {

	unsigned int i, j;
	double symbols = (double)m_count / (double)m_sps;

	// we can detect at most m_avg_n - 1 sequential values
	for(i = 1; (i < m_avg_n - 1) && ((double)i - m_error < symbols); i++) {
		if(symbols <= ((double)i + m_error)) {
			// valid symbol

			// if first valid symbol in burst, save start
			if(!m_rx_buf_count) {
				m_rx_last_buf_received = m_rx_buf_received;
				m_rx_buf_received = m_rx_sample_number - (m_count + m_jitter + 1 + 2 * m_average_len);
			}

			for(j = 0; j < i; j++) {
				m_rx_buf[m_rx_buf_count++] = (m_sign >= 0);

				// if demodulated buffer is full, decode symbols
				if(m_rx_buf_count >= sizeof(m_rx_buf)) {
					decode_rx_symbols();
				}
			}

			return;
		}
	}

	/*
	 * Half-symbol logic guesses:
	 *
	 * A half-symbol indicates a violation and usually separates the
	 * preamble and data.
	 *
	 * A half-symbol never occurs in the center of a bit.  (I.e.,
	 * between two symbols that represent a bit.)
	 *
	 * I'd like to assume that a violation always continues the last
	 * transmitted symbol, but I'm not positive.
	 *
	 * Only .5, 1.5, and 2.5 widths could possibly be transmitted
	 * normally for otherwise a bit was transmitted without a phase
	 * transition.
	 */

	// detect half-symbols
	for(i = 0; (i <= 2) && ((double)i + 0.5 - m_error < symbols); i++) {
		if(symbols <= ((double)i + 0.5 + m_error)) {
			// valid half-symbols

			// if first valid symbol in burst, save start
			if(!m_rx_buf_count) {
				m_rx_last_buf_received = m_rx_buf_received;
				m_rx_buf_received = m_rx_sample_number - (m_count + m_jitter + 1 + 2 * m_average_len);
			}

			m_rx_buf[m_rx_buf_count++] = (i + 1) * 2 + (m_sign >= 0);

			// if full, decode symbols
			if(m_rx_buf_count >= sizeof(m_rx_buf)) {
				decode_rx_symbols();
			}

			return;
		}
	}

	// this width did not match valid symbols
	if(m_rx_buf_count > 0) {
		/*
		 * Since we have valid data and this is the first place we
		 * errored out, we process this data.
		 */
		decode_rx_symbols();
	}

	return;
}


void omnipod_pda::process_rx_sample(float cur) {

	double avg;

	/*
	if(m_rx_buf_count <= 2 * m_avg_n) {
		avg = m_average_a / m_average_len;
	} else {
		avg = m_average_b / m_average_len;
	}
	 */

	if(m_rx_buf_count <= m_avg_n) {
		avg = m_average_a / m_average_len;
	} else {
		avg = m_average_b / m_average_len;
	}

	/*
	 * If we've gone too long without slice(), this isn't a valid symbol.
	 * Decode what we have as quick as possible.
	 */
	if(m_count > m_avg_n * m_sps) {
		if(m_rx_buf_count > 0)
			decode_rx_symbols();
	}

	if(cur < avg) {
		if(m_sign < 0) {
			m_count += m_change_count + 1;
			m_change_count = 0;
		} else {
			// swapped from high to low
			if(m_change_count < m_jitter) {
				m_change_count += 1;
			} else {
				slice();
				m_sign = -1;
				m_count = m_change_count + 1;
				m_change_count = 0;
			}
		}
	} else {
		if(m_sign > 0) {
			m_count += m_change_count + 1;
			m_change_count = 0;
		} else {
			// swapped from low to high
			if(m_change_count < m_jitter) {
				m_change_count += 1;
			} else {
				slice();
				m_sign = 1;
				m_count = m_change_count + 1;
				m_change_count = 0;
			}
		}
	}
}


void omnipod_pda::process_decoded() {

	// XXX
	if(m_rx_decoded) {
		delete[] m_rx_decoded;
		m_rx_decoded = 0;
		m_rx_decoded_len = 0;
		m_rx_decoded_received = 0;
	}
}


unsigned int omnipod_pda::process_tx(gr_complex *output, int noutput) {

	unsigned int i;

	if(!m_tx_buf) {
		printf("called process_tx with null m_tx_buf\n");
		return 0;
	}

	for(i = 0; (i + m_tx_buf_cur < m_tx_buf_count) && ((int)i < noutput); i++)
		output[i] = m_tx_buf[i + m_tx_buf_cur];
	m_tx_buf_cur += i;
	m_tx_sample_number += i;
	if(m_tx_buf_cur >= m_tx_buf_count) {
		m_retransmit_num += 1;

		display_data("Transmit %d", m_retransmit_num);

		// set up retransmit
		if(m_retransmit_num < m_retransmit_max) {
			// XXX how fast can we do this?
			m_tx_at = m_rx_sample_number + (unsigned long long)(250.0 * m_sr / 1000.0);
			m_tx_buf_cur = 0;
			display_data("Rescheduled for %llu", m_tx_at);
		} else {
			delete[] m_tx_buf;
			m_tx_buf = 0;
			m_tx_buf_count = 0;
			m_tx_buf_cur = 0;
			m_tx_at = m_at_never;
			m_retransmit_num = 0;
			m_state = ST_IDLE;
			display_data("Retransmit finished");
			display_status("Exceeded retries");
		}
	}

	return i;
}


void omnipod_pda::transmit_packet(char *data, unsigned int data_len) {

	unsigned int i;
	gr_complex *p;


	if(m_tx_buf) {
		delete[] m_tx_buf;
		m_tx_buf_count = 0;
		m_tx_buf_cur = 0;
	}

	if(!(m_tx_buf = new gr_complex[data_len * m_bitlen])) {
		fprintf(stderr, "error: cannot create tx buf\n");
		return;
	}
	for(i = 0, p = m_tx_buf; i < data_len; i++) {
		switch(data[i]) {
			case '0':
				memcpy(p, m_zero, m_bitlen * sizeof(gr_complex));
				p += m_bitlen;
				break;
			case '1':
				memcpy(p, m_one, m_bitlen * sizeof(gr_complex));
				p += m_bitlen;
				break;
			case '^':
				memcpy(p, m_hv, m_hv_len * sizeof(gr_complex));
				p += m_hv_len;
				break;
			case 'v':
				memcpy(p, m_lv, m_lv_len * sizeof(gr_complex));
				p += m_lv_len;
				break;
			case 'S':
				memset(p, 0, m_bitlen * sizeof(gr_complex));
				p += m_bitlen;
				break;
			default:
				fprintf(stderr, "error: cannot transmit symbol: ''%c''\n", data[i]);
				break;
		}
	}
	m_tx_buf_count = p - m_tx_buf;
	m_tx_buf_cur = 0;
	m_tx_at = 0;
}


static void i8tob(unsigned char c, char *b) {

	int i;

	for(i = 0; i < 8; i++)
		b[i] = '0' + ((c >> (8 - 1 - i)) & 1);
}


static void do_put(char *packet, unsigned int packet_len, unsigned int &offset, const char *data, unsigned int data_len) {

	if(data_len + offset > packet_len)
		return;
	memcpy(packet + offset, data, data_len);
	offset += data_len;
}


void omnipod_pda::transmit_on_packet() {

	static const char *start =	"1110101011";
	static const char *ab	=	"10101011";
	static const char *three =	"0011";
	static const char *seven =	"0111";
	static const char *b =		"1011";
	static const char *f =		"1111";

	int i, j, repeat;
	// char secret_bits[4][8], data[1024];
	char secret_bits[4][8], data[1024];
	// char secret_bits[4][8], data[20 * 1024];
	unsigned int offset = 0, data_len = sizeof(data);

	// it looks like this is sent 10 times before they give up
	// let's schedule them all at once.

	for(i = 0; i < 4; i++)
		i8tob((m_secret >> ((4 - 1 - i) * 8)) & 0xff, secret_bits[i]);

	for(i = 0; i < 10; i++) {
		do_put(data, data_len, offset, start, 10);
		for(repeat = 0; repeat < 17; repeat++) {
			do_put(data, data_len, offset, "v", 1);
			do_put(data, data_len, offset, secret_bits[1], 8);
			do_put(data, data_len, offset, three, 4);
			do_put(data, data_len, offset, ab, 8);

			do_put(data, data_len, offset, "v", 1);
			do_put(data, data_len, offset, secret_bits[0], 8);
			do_put(data, data_len, offset, seven, 4);
			do_put(data, data_len, offset, ab, 8);

			do_put(data, data_len, offset, "v", 1);
			do_put(data, data_len, offset, secret_bits[3], 8);
			do_put(data, data_len, offset, b, 4);
			do_put(data, data_len, offset, ab, 8);

			do_put(data, data_len, offset, "v", 1);
			do_put(data, data_len, offset, secret_bits[2], 8);
			do_put(data, data_len, offset, f, 4);
			do_put(data, data_len, offset, ab, 8);
		}

		// 250ms of silence
		for(j = 0; j < (int)((250.0 * (m_sps / 1000.0)) / m_bitlen); j++)
			do_put(data, data_len, offset, "S", 1);
	}
	data[offset] = 0;

	transmit_packet(data, offset);

	m_state = ST_STATUS_ON_SENT;
}


int omnipod_pda::general_work(int noutput_items, gr_vector_int &ninput_items, gr_vector_const_void_star &input_items, gr_vector_void_star &output_items) {

	static int starting_now = 1;

	int ninput = ninput_items[0], noutput = noutput_items;
	const gr_complex *input = (const gr_complex *)input_items[0];
	gr_complex *output = (gr_complex *)output_items[0];

	unsigned int r = 0;
	int w = 0, tx_enabled, monitor;
	float cur;
	e_state state;

	if(starting_now) {
		if(ninput < (int)(2 * m_average_len + 1))
			return 0;
		m_average_a = 0;
		m_average_b = 0;
		for(unsigned int i = 0; i < m_average_len; i++) {
			m_average_a += std::abs(input[m_average_len + 1 + i]);
			m_average_b += std::abs(input[i]);
		}
		m_rx_sample_number = m_average_len;
		starting_now = 0;
	}

	// only check this once per call
	state = get_state();
	monitor = get_monitor();

	for(r = 0; (int)(r + 2 * m_average_len + 1) < ninput; r++) {

		m_rx_sample_number += 1;

		// running averages
		cur = std::abs(input[r + m_average_len + 1]);
		m_average_a = m_average_a - cur + std::abs(input[r + 2 * m_average_len + 1]);
		m_average_b = m_average_b - std::abs(input[r]) + std::abs(input[r + m_average_len]);

		if((state != ST_IDLE) || (monitor)) {
			process_rx_sample(cur);
		}

		/*
		printf("sample number: %llu\taverage_a: %lf\taverage_b: %lf\tcur: %f\tcount: %u (%u)\n", m_rx_sample_number, m_average_a / m_average_len, m_average_b / m_average_len, cur, m_count,
		   m_change_count);
		 */

		if(state != ST_IDLE) {
			switch(state) {
				case ST_STATUS:
					// we are just starting the status sequence
					state = ST_STATUS_ON_SENT;
					transmit_on_packet();
					break;
				default:
					break;
			}

			if(m_rx_decoded) {
				process_decoded();
			}

			if((m_tx_at < m_tx_sample_number) && (w < noutput)) {
				w += process_tx(output + w, noutput - w);
			}
		}
	}

	// try to keep TX from underflow
	// while((m_tx_sample_number < m_rx_sample_number + 1024) && (w < noutput)) {
	while((m_tx_sample_number < m_rx_sample_number) && (w < noutput)) {
		output[w] = 0;
		w += 1;
		m_tx_sample_number += 1;
	}

	/*
	printf("ninput: %d\tprocessed: %u\tremain: %d\trsn: %llu\tnoutput: %d\tprocessed: %d\tremain: %d\ttsn: %llu\t\tdiff: %lld",
	   ninput, r, ninput - r, m_rx_sample_number, noutput, w, noutput - w, m_tx_sample_number, m_rx_sample_number - m_tx_sample_number);
	 */

	consume(0, r);
	produce(0, w);

	return WORK_CALLED_PRODUCE;
}
