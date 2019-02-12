#include <stdio.h>
#include <string.h>


static int do_put(char *buf, unsigned int bufsize, unsigned int &o, const char *c) {

	unsigned int s = strlen(c);

	if(o + s < bufsize - 1) {
		memcpy(buf + o, c, s);
		o += s;
		return 0;
	}
	return 1;
}


unsigned int manchester_decode(unsigned char *dbuf, unsigned int dbuf_count, char *data, unsigned int max_data_len) {

	unsigned int data_len, i;

	data_len = 0;
	for(i = 0; i < dbuf_count - 1;) {
		switch(dbuf[i]) {
			case 0: // 0
				switch(dbuf[i + 1]) {
					case 0:		// 0 0		error no phase change; perhaps missed first symbol
						do_put(data, max_data_len, data_len, "*");
						i += 1;
						break;
					case 1:		// 0 1
						do_put(data, max_data_len, data_len, "0");
						i += 2;
						break;
					case 2:		// 0 v		impossible error
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 3:		// 0 ^		error violation in center; perhaps missed first symbol
						do_put(data, max_data_len, data_len, "*");
						i += 1;
						break;
					case 4:		// 0 0 v	impossible error
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 5:		// 0 1 ^
						do_put(data, max_data_len, data_len, "0^");
						i += 2;
						break;
					case 6:		// 0 0 v 0	impossible error
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 7:		// 0 1 ^ 1
						do_put(data, max_data_len, data_len, "0^");
						dbuf[i + 1] = 1;
						i += 1;
						break;
					default:
						do_put(data, max_data_len, data_len, "X");
						i += 2;
				}
				break;

			case 1: // 1
				switch(dbuf[i + 1]) {
					case 0:		// 1 0
						do_put(data, max_data_len, data_len, "1");
						i += 2;
						break;
					case 1:		// 1 1		error no phase change; perhaps missed first symbol
						do_put(data, max_data_len, data_len, "*");
						i += 1;
						break;
					case 2:		// 1 v		error violation in center; perhaps missed first symbol
						do_put(data, max_data_len, data_len, "*");
						i += 1;
						break;
					case 3:		// 1 ^		impossible error
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 4:		// 1 0 v
						do_put(data, max_data_len, data_len, "1v");
						i += 2;
						break;
					case 5:		// 1 1 ^	impossible error
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 6:		// 1 0 v 0
						do_put(data, max_data_len, data_len, "1v");
						dbuf[i + 1] = 0;
						i += 1;
						break;
					case 7:		// 1 1 ^ 1	impossible error
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					default:
						do_put(data, max_data_len, data_len, "X");
						i += 2;
				}
				break;

			case 2: // v
				do_put(data, max_data_len, data_len, "v");
				i += 1;
				break;

			case 3: // ^
				do_put(data, max_data_len, data_len, "^");
				i += 1;
				break;

			case 4: // v 0	-- since first, assuming violation comes before symbol
				switch(dbuf[i + 1]) {
					case 0:		// v 0 0	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 1:		// v 0 1
						do_put(data, max_data_len, data_len, "v0");
						i += 2;
						break;
					case 2:		// v 0 v	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 3:		// v 0 ^	error violation in center
						do_put(data, max_data_len, data_len, "v*");
						i += 1;
						break;
					case 4:		// v 0 0 v	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 5:		// v 0 1 ^
						do_put(data, max_data_len, data_len, "v0^");
						i += 2;
						break;
					case 6:		// v 0 0 v 0	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 7:		// v 0 1 ^ 1
						do_put(data, max_data_len, data_len, "v0^");
						dbuf[i + 1] = 1;
						i += 1;
						break;
					default:
						do_put(data, max_data_len, data_len, "X");
						i += 2;
				}
				break;

			case 5: // ^ 1
				switch(dbuf[i + 1]) {
					case 0:		// ^ 1 0
						do_put(data, max_data_len, data_len, "^1");
						i += 2;
						break;
					case 1:		// ^ 1 1	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 2:		// ^ 1 v	error violation in center
						do_put(data, max_data_len, data_len, "^*");
						i += 1;
						break;
					case 3:		// ^ 1 ^	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 4:		// ^ 1 0 v
						do_put(data, max_data_len, data_len, "^1v");
						i += 2;
						break;
					case 5:		// ^ 1 1 ^	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 6:		// ^ 1 0 v 0
						do_put(data, max_data_len, data_len, "^1v");
						dbuf[i + 1] = 0;
						i += 1;
						break;
					case 7:		// ^ 1 1 ^ 1	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					default:
						do_put(data, max_data_len, data_len, "X");
						i += 2;
				}
				break;

			case 6: // 0 v 0
				switch(dbuf[i + 1]) {
					case 0:		// 0 v 0 0	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 1:		// 0 v 0 1	error violation in center
						do_put(data, max_data_len, data_len, "*v0");
						i += 2;
						break;
					case 2:		// 0 v 0 v	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 3:		// 0 v 0 ^	error violation in center
						do_put(data, max_data_len, data_len, "*");
						i += 1;
						break;
					case 4:		// 0 v 0 0 v	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 5:		// 0 v 0 1 v	error violation in center
						do_put(data, max_data_len, data_len, "*v0v");
						i += 2;
						break;
					case 6:		// 0 v 0 0 v 0	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 7:		// 0 v 0 1 ^ 1	error violation in center
						do_put(data, max_data_len, data_len, "*v0^");
						dbuf[i + 1] = 1;
						i += 1;
						break;
					default:
						do_put(data, max_data_len, data_len, "X");
						i += 2;
				}
				break;

			case 7: // 1 ^ 1
				switch(dbuf[i + 1]) {
					case 0:		// 1 ^ 1 0	error violation in center
						do_put(data, max_data_len, data_len, "*^1");
						i += 2;
						break;
					case 1:		// 1 ^ 1 1 	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 2:		// 1 ^ 1 v	error violation in center
						do_put(data, max_data_len, data_len, "*");
						i += 1;
						break;
					case 3:		// 1 ^ 1 ^	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 4:		// 1 ^ 1 0 v	error violation in center
						do_put(data, max_data_len, data_len, "*^1v");
						i += 2;
						break;
					case 5:		// 1 ^ 1 1 ^	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					case 6:		// 1 ^ 1 0 v 0	error violation in center
						do_put(data, max_data_len, data_len, "*^1v");
						dbuf[i + 1] = 0;
						i += 1;
						break;
					case 7:		// 1 ^ 1 1 ^ 1	impossible
						do_put(data, max_data_len, data_len, "#");
						i += 1;
						break;
					default:
						do_put(data, max_data_len, data_len, "X");
						i += 2;
				}
				break;

			default:
				do_put(data, max_data_len, data_len, "X");
				i += 1;
		}
	}
	data[data_len] = 0;

	return data_len;
}
